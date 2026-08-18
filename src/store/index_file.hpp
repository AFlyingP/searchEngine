#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "invidx/postings.hpp"
#include "invidx/radix_trie.hpp"
#include "store/mmap.hpp"

namespace needlefish {

constexpr char INDEX_MAGIC[8] = {'N', 'F', 'L', 'S', 'H', 'I', 'D', 'X'};
constexpr uint32_t INDEX_VERSION = 1;

enum class SectionId : uint32_t {
    Stats = 1,
    DocMetadata = 2,
    StoredFields = 3,
    TermDict = 4,
    Postings = 5,
    Positions = 6,
    FmIndex = 7
};

struct SectionEntry {
    uint32_t section_id{0};
    uint32_t reserved{0};
    uint64_t offset{0};
    uint64_t length{0};
    uint32_t checksum{0};
};

struct alignas(8) IndexHeader {
    char magic[8]{'N', 'F', 'L', 'S', 'H', 'I', 'D', 'X'};
    uint32_t version{INDEX_VERSION};
    uint32_t num_sections{0};
    uint64_t file_size{0};
};

struct alignas(8) Bm25Stats {
    uint32_t total_docs{0};
    uint64_t total_tokens{0};
    double avg_doc_len{0.0};
};

struct alignas(8) DocMetadataRecord {
    uint32_t doc_id{0};
    uint32_t token_count{0};
    uint64_t title_offset{0};
    uint32_t title_len{0};
    uint64_t text_offset{0};
    uint32_t text_len{0};
};

/**
 * @brief Zero-copy Memory-Mapped Search Engine Index View.
 * Provides lock-free, zero-copy span access to all index structures.
 */
class IndexView {
  public:
    IndexView() = default;
    explicit IndexView(const std::filesystem::path& path);

    void open(const std::filesystem::path& path);
    void open_from_bytes(std::span<const uint8_t> bytes);

    [[nodiscard]] const Bm25Stats& stats() const noexcept { return stats_; }
    [[nodiscard]] size_t total_docs() const noexcept { return stats_.total_docs; }
    [[nodiscard]] double avg_doc_len() const noexcept { return stats_.avg_doc_len; }

    [[nodiscard]] const DocMetadataRecord& doc_metadata(uint32_t doc_id) const;
    [[nodiscard]] std::string_view doc_title(uint32_t doc_id) const;
    [[nodiscard]] std::string_view doc_text(uint32_t doc_id) const;

    [[nodiscard]] const RadixTrie& term_dict() const noexcept { return trie_; }
    [[nodiscard]] PostingListReader get_posting_reader(const TermPayload& payload) const;

    [[nodiscard]] std::span<const uint8_t> postings_section() const noexcept {
        return postings_span_;
    }
    [[nodiscard]] std::span<const uint8_t> positions_section() const noexcept {
        return positions_span_;
    }
    [[nodiscard]] std::span<const uint8_t> stored_fields_section() const noexcept {
        return stored_fields_span_;
    }

  private:
    void parse_sections(std::span<const uint8_t> data);

    MemoryMappedFile mmap_{};
    Bm25Stats stats_{};
    std::span<const DocMetadataRecord> doc_records_{};
    std::span<const uint8_t> stored_fields_span_{};
    std::span<const uint8_t> postings_span_{};
    std::span<const uint8_t> positions_span_{};
    RadixTrie trie_{};
};

}  // namespace needlefish
