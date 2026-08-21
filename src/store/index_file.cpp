#include "store/index_file.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace needlefish {

namespace {

uint32_t compute_crc32(std::span<const uint8_t> data) {
    // Standard CRC-32 (IEEE 802.3)
    uint32_t crc = 0xFFFFFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

}  // namespace

IndexView::IndexView(const std::filesystem::path& path) {
    open(path);
}

void IndexView::open(const std::filesystem::path& path) {
    mmap_.open(path);
    parse_sections(mmap_.bytes());
}

void IndexView::open_from_bytes(std::span<const uint8_t> bytes) {
    parse_sections(bytes);
}

void IndexView::parse_sections(std::span<const uint8_t> data) {
    if (data.size() < sizeof(IndexHeader)) {
        throw std::runtime_error("Corrupted index: file too small for header");
    }

    IndexHeader header;
    std::memcpy(&header, data.data(), sizeof(header));

    if (std::memcmp(header.magic, INDEX_MAGIC, sizeof(INDEX_MAGIC)) != 0) {
        throw std::runtime_error("Corrupted index: invalid magic bytes");
    }
    if (header.version != INDEX_VERSION) {
        throw std::runtime_error("Unsupported index version: " + std::to_string(header.version));
    }
    if (header.file_size != 0 && header.file_size != data.size()) {
        throw std::runtime_error("Corrupted index: header file_size mismatch");
    }

    const size_t section_table_size = header.num_sections * sizeof(SectionEntry);
    if (header.num_sections > 1000 || section_table_size > data.size() - sizeof(IndexHeader)) {
        throw std::runtime_error("Corrupted index: invalid section table size");
    }

    // Precompute whole-index metadata checksum
    index_checksum_ = compute_crc32(data.subspan(0, sizeof(IndexHeader) + section_table_size));

    std::vector<SectionEntry> sections(header.num_sections);
    std::memcpy(sections.data(), data.data() + sizeof(IndexHeader), section_table_size);

    std::vector<uint32_t> seen_sections;
    for (const auto& sec : sections) {
        if (std::find(seen_sections.begin(), seen_sections.end(), sec.section_id) != seen_sections.end()) {
            throw std::runtime_error("Corrupted index: duplicate section ID " + std::to_string(sec.section_id));
        }
        seen_sections.push_back(sec.section_id);

        if (sec.offset > data.size() || sec.length > data.size() - sec.offset) {
            throw std::runtime_error("Corrupted index: section bounds out of range or integer overflow");
        }

        std::span<const uint8_t> sec_data(data.data() + sec.offset, sec.length);

        // Validate section checksum (skip only if section length is 0)
        if (sec.length > 0) {
            const uint32_t actual_crc = compute_crc32(sec_data);
            if (actual_crc != sec.checksum) {
                throw std::runtime_error("Corrupted index: checksum mismatch in section " +
                                         std::to_string(sec.section_id));
            }
        }

        switch (static_cast<SectionId>(sec.section_id)) {
            case SectionId::Stats:
                if (sec_data.size() < sizeof(Bm25Stats)) {
                    throw std::runtime_error("Corrupted stats section");
                }
                std::memcpy(&stats_, sec_data.data(), sizeof(Bm25Stats));
                break;
            case SectionId::DocMetadata: {
                const size_t num_records = sec_data.size() / sizeof(DocMetadataRecord);
                doc_records_ = std::span<const DocMetadataRecord>(
                    reinterpret_cast<const DocMetadataRecord*>(sec_data.data()), num_records);
                break;
            }
            case SectionId::StoredFields:
                stored_fields_span_ = sec_data;
                break;
            case SectionId::TermDict: {
                std::string str_view(reinterpret_cast<const char*>(sec_data.data()),
                                     sec_data.size());
                std::stringstream ss(str_view);
                trie_ = RadixTrie::deserialize(ss);
                break;
            }
            case SectionId::Postings:
                postings_span_ = sec_data;
                break;
            case SectionId::Positions:
                positions_span_ = sec_data;
                break;
            case SectionId::FmIndex: {
                std::string str_view(reinterpret_cast<const char*>(sec_data.data()),
                                     sec_data.size());
                std::stringstream ss(str_view);
                fm_index_ = std::make_unique<FMIndex>(FMIndex::deserialize(ss));
                break;
            }
            default:
                break;
        }
    }
}

uint32_t IndexView::external_id(uint32_t internal_id) const noexcept {
    if (internal_id >= doc_records_.size()) {
        return internal_id;
    }
    return doc_records_[internal_id].doc_id;
}

const DocMetadataRecord& IndexView::doc_metadata(uint32_t doc_id) const {
    if (doc_id >= doc_records_.size()) {
        throw std::out_of_range("doc_id out of range in index");
    }
    return doc_records_[doc_id];
}

std::string_view IndexView::doc_title(uint32_t doc_id) const {
    const auto& meta = doc_metadata(doc_id);
    if (meta.title_offset > stored_fields_span_.size() ||
        meta.title_len > stored_fields_span_.size() - meta.title_offset) {
        return "";
    }
    return std::string_view(
        reinterpret_cast<const char*>(stored_fields_span_.data() + meta.title_offset),
        meta.title_len);
}

std::string_view IndexView::doc_text(uint32_t doc_id) const {
    const auto& meta = doc_metadata(doc_id);
    if (meta.text_offset > stored_fields_span_.size() ||
        meta.text_len > stored_fields_span_.size() - meta.text_offset) {
        return "";
    }
    return std::string_view(
        reinterpret_cast<const char*>(stored_fields_span_.data() + meta.text_offset),
        meta.text_len);
}

PostingListReader IndexView::get_posting_reader(const TermPayload& payload) const {
    if (!payload.valid() || payload.postings_offset >= postings_span_.size()) {
        return PostingListReader{};
    }

    const size_t available = postings_span_.size() - payload.postings_offset;
    const size_t bytes_to_read = (payload.postings_bytes > 0)
                                     ? std::min<size_t>(payload.postings_bytes, available)
                                     : available;
    std::span<const uint8_t> post_span =
        postings_span_.subspan(payload.postings_offset, bytes_to_read);
    return PostingListReader(post_span, positions_span_, payload.doc_freq);
}

}  // namespace needlefish
