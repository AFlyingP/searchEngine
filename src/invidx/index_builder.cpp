#include "invidx/index_builder.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "rank/bm25.hpp"

namespace needlefish {

namespace {

uint32_t compute_crc32(std::span<const uint8_t> data) {
    uint32_t crc = 0xFFFFFFFF;
    for (uint8_t byte : data) {
        crc ^= byte;
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

// Lightweight hand-written JSON string unescaper
std::string parse_json_string(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\' && i + 1 < raw.size()) {
            char next = raw[i + 1];
            if (next == '"') {
                out.push_back('"');
                i++;
            } else if (next == '\\') {
                out.push_back('\\');
                i++;
            } else if (next == 'n') {
                out.push_back('\n');
                i++;
            } else if (next == 't') {
                out.push_back('\t');
                i++;
            } else if (next == 'r') {
                out.push_back('\r');
                i++;
            } else {
                out.push_back(next);
                i++;
            }
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

// Fast JSONL line extractor for {"id": ..., "title": "...", "text": "..."}
bool extract_jsonl_fields(std::string_view line, uint32_t& doc_id, std::string& title,
                          std::string& text) {
    auto find_field = [&](std::string_view key) -> std::string_view {
        const size_t kpos = line.find(key);
        if (kpos == std::string_view::npos) {
            return "";
        }
        size_t colon = line.find(':', kpos + key.size());
        if (colon == std::string_view::npos) {
            return "";
        }
        size_t val_start = line.find_first_not_of(" \t\r\n", colon + 1);
        if (val_start == std::string_view::npos) {
            return "";
        }

        if (line[val_start] == '"') {
            // Quoted string
            size_t curr = val_start + 1;
            while (curr < line.size()) {
                if (line[curr] == '"' && line[curr - 1] != '\\') {
                    break;
                }
                curr++;
            }
            return line.substr(val_start + 1, curr - (val_start + 1));
        } else {
            // Number or bare token
            size_t val_end = line.find_first_of(",}\r\n ", val_start);
            if (val_end == std::string_view::npos)
                val_end = line.size();
            return line.substr(val_start, val_end - val_start);
        }
    };

    auto id_str = find_field("\"id\"");
    if (id_str.empty()) {
        id_str = find_field("id");
    }
    auto title_str = find_field("\"title\"");
    if (title_str.empty()) {
        title_str = find_field("title");
    }
    auto text_str = find_field("\"text\"");
    if (text_str.empty()) {
        text_str = find_field("text");
    }

    if (id_str.empty() && text_str.empty()) {
        return false;
    }

    try {
        doc_id = static_cast<uint32_t>(std::stoul(std::string(id_str)));
    } catch (...) {
        doc_id = 0;
    }

    title = parse_json_string(title_str);
    text = parse_json_string(text_str);
    return true;
}

}  // namespace

IndexBuilder::IndexBuilder(size_t memory_budget_bytes) : memory_budget_(memory_budget_bytes) {}

void IndexBuilder::add_document(uint32_t doc_id, std::string_view title, std::string_view text) {
    accumulate_document(doc_id, title, text);
}

void IndexBuilder::accumulate_document(uint32_t doc_id, std::string_view title,
                                       std::string_view text) {
    const uint32_t internal_id = static_cast<uint32_t>(doc_metadata_.size());

    // 1. Store title and text in stored fields buffer
    const uint64_t title_offset = stored_fields_.size();
    stored_fields_.insert(stored_fields_.end(), title.begin(), title.end());
    const uint32_t title_len = static_cast<uint32_t>(title.size());

    const uint64_t text_offset = stored_fields_.size();
    stored_fields_.insert(stored_fields_.end(), text.begin(), text.end());
    const uint32_t text_len = static_cast<uint32_t>(text.size());

    // 2. Tokenize and analyze
    std::string combined = std::string(title) + " " + std::string(text);
    auto tokens = analyzer_.analyze(combined);
    const uint32_t token_count = static_cast<uint32_t>(tokens.size());
    total_tokens_ += token_count;

    doc_metadata_.push_back(DocMetadataRecord{
        .doc_id = doc_id,
        .token_count = token_count,
        .title_offset = title_offset,
        .title_len = title_len,
        .text_offset = text_offset,
        .text_len = text_len
    });

    // 3. Accumulate term postings using internal contiguous doc_id
    std::map<std::string, std::vector<uint32_t>> doc_term_positions;
    for (const auto& tok : tokens) {
        doc_term_positions[tok.term].push_back(tok.position);
    }

    for (auto& [term, pos_vec] : doc_term_positions) {
        auto& p_list = term_postings_[term];
        p_list.push_back(DocPosting{
            .doc_id = internal_id,
            .term_freq = static_cast<uint32_t>(pos_vec.size()),
            .positions = std::move(pos_vec)
        });
    }

    estimated_memory_usage_ = stored_fields_.size() +
                              doc_metadata_.size() * sizeof(DocMetadataRecord) +
                              term_postings_.size() * 64;
}

size_t IndexBuilder::index_jsonl_file(const std::filesystem::path& jsonl_path) {
    std::ifstream infile(jsonl_path);
    if (!infile.is_open()) {
        throw std::runtime_error("Could not open JSONL file: " + jsonl_path.string());
    }

    std::string line;
    size_t count = 0;
    while (std::getline(infile, line)) {
        if (line.empty()) {
            continue;
        }
        uint32_t external_doc_id = 0;
        std::string title;
        std::string text;
        if (extract_jsonl_fields(line, external_doc_id, title, text)) {
            const uint32_t internal_id = static_cast<uint32_t>(doc_metadata_.size());
            // If external_doc_id was 0, use internal_id + 1
            const uint32_t final_ext_id = (external_doc_id != 0) ? external_doc_id : (internal_id + 1);
            accumulate_document(final_ext_id, title, text);
            count++;
        }
    }
    return count;
}

void IndexBuilder::write_index(const std::filesystem::path& output_idx_path) {
    std::ofstream out(output_idx_path, std::ios::binary);
    if (!out.is_open()) {
        throw std::runtime_error("Failed to open output file: " + output_idx_path.string());
    }

    const uint32_t num_docs = static_cast<uint32_t>(doc_metadata_.size());
    const double avg_doc_len = (num_docs > 0) ? static_cast<double>(total_tokens_) / num_docs : 0.0;

    // 1. Build postings and positions sections and construct RadixTrie
    std::vector<uint8_t> postings_buf;
    std::vector<uint8_t> positions_buf;
    RadixTrie trie;

    uint32_t term_id_seq = 0;
    for (const auto& [term, postings] : term_postings_) {
        const uint64_t post_offset = postings_buf.size();
        const uint32_t doc_freq = static_cast<uint32_t>(postings.size());

        // Calculate max BM25 score for this term across its postings
        const double idf = BM25Scorer::compute_idf(doc_freq, num_docs);
        float max_term_score = 0.0f;
        for (const auto& post : postings) {
            const uint32_t doc_len = (post.doc_id < doc_metadata_.size())
                                         ? doc_metadata_[post.doc_id].token_count
                                         : static_cast<uint32_t>(avg_doc_len);
            const float score = BM25Scorer::score_tf(post.term_freq, doc_len, avg_doc_len, idf);
            if (score > max_term_score) {
                max_term_score = score;
            }
        }

        PostingListWriter writer;
        for (const auto& post : postings) {
            writer.add_posting(post.doc_id, post.term_freq, post.positions);
        }
        writer.finish(postings_buf, positions_buf, max_term_score);

        const uint32_t post_bytes = static_cast<uint32_t>(postings_buf.size() - post_offset);

        TermPayload payload{.term_id = term_id_seq++,
                            .doc_freq = doc_freq,
                            .postings_offset = post_offset,
                            .postings_bytes = post_bytes,
                            .max_term_score = max_term_score};
        trie.insert(term, payload);
    }

    // 2. Serialize TermDict (RadixTrie)
    std::stringstream trie_ss;
    trie.serialize(trie_ss);
    const std::string trie_str = trie_ss.str();
    std::vector<uint8_t> term_dict_buf(trie_str.begin(), trie_str.end());

    // 3. Prepare Stats Section
    Bm25Stats stats{
        .total_docs = num_docs, .total_tokens = total_tokens_, .avg_doc_len = avg_doc_len};
    std::vector<uint8_t> stats_buf(sizeof(stats));
    std::memcpy(stats_buf.data(), &stats, sizeof(stats));

    // 4. Prepare Doc Metadata Section
    std::vector<uint8_t> doc_meta_buf(doc_metadata_.size() * sizeof(DocMetadataRecord));
    std::memcpy(doc_meta_buf.data(), doc_metadata_.data(), doc_meta_buf.size());

    // 5. Build section table and align all sections to 64 bytes
    struct SectionDef {
        SectionId id;
        const std::vector<uint8_t>& data;
    };

    std::vector<SectionDef> sec_defs = {{SectionId::Stats, stats_buf},
                                        {SectionId::DocMetadata, doc_meta_buf},
                                        {SectionId::StoredFields, stored_fields_},
                                        {SectionId::TermDict, term_dict_buf},
                                        {SectionId::Postings, postings_buf},
                                        {SectionId::Positions, positions_buf}};

    std::vector<uint8_t> fm_index_buf;
    if (enable_fm_index_ && !stored_fields_.empty()) {
        FMIndex fm_idx(stored_fields_);
        std::stringstream fm_ss;
        fm_idx.serialize(fm_ss);
        const std::string fm_str = fm_ss.str();
        fm_index_buf.assign(fm_str.begin(), fm_str.end());
        sec_defs.push_back({SectionId::FmIndex, fm_index_buf});
    }

    const uint32_t num_sections = static_cast<uint32_t>(sec_defs.size());
    const size_t header_and_table_size = sizeof(IndexHeader) + num_sections * sizeof(SectionEntry);

    // Round up to 64-byte alignment for first section
    size_t current_offset = (header_and_table_size + 63) / 64 * 64;

    std::vector<SectionEntry> section_entries;
    for (const auto& s : sec_defs) {
        const uint64_t len = s.data.size();
        const uint32_t crc = compute_crc32(s.data);

        section_entries.push_back(SectionEntry{.section_id = static_cast<uint32_t>(s.id),
                                               .reserved = 0,
                                               .offset = current_offset,
                                               .length = len,
                                               .checksum = crc});

        current_offset = (current_offset + len + 63) / 64 * 64;
    }

    const uint64_t total_file_size = current_offset;

    IndexHeader header;
    std::memcpy(header.magic, INDEX_MAGIC, sizeof(INDEX_MAGIC));
    header.version = INDEX_VERSION;
    header.num_sections = num_sections;
    header.file_size = total_file_size;

    // Write Header
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    // Write Section Table
    out.write(reinterpret_cast<const char*>(section_entries.data()),
              static_cast<std::streamsize>(num_sections * sizeof(SectionEntry)));

    // Pad to first section
    size_t written = sizeof(header) + num_sections * sizeof(SectionEntry);
    while (written < section_entries[0].offset) {
        const char zero = 0;
        out.write(&zero, 1);
        written++;
    }

    // Write each section with 64-byte padding
    for (size_t i = 0; i < sec_defs.size(); ++i) {
        out.write(reinterpret_cast<const char*>(sec_defs[i].data.data()),
                  static_cast<std::streamsize>(sec_defs[i].data.size()));
        written += sec_defs[i].data.size();

        const size_t target_offset =
            (i + 1 < section_entries.size()) ? section_entries[i + 1].offset : total_file_size;
        while (written < target_offset) {
            const char zero = 0;
            out.write(&zero, 1);
            written++;
        }
    }
}

}  // namespace needlefish
