#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "store/index_file.hpp"
#include "util/analyzer.hpp"

namespace needlefish {

struct DocumentInput {
    uint32_t doc_id{0};
    std::string title{};
    std::string text{};
};

/**
 * @brief Inverted Index Builder.
 * Supports streaming documents, memory-budgeted segment accumulation,
 * and immutable single-file .idx output generation.
 */
class IndexBuilder {
  public:
    IndexBuilder() = default;

    /**
     * @brief Add a document to the index builder.
     */
    void add_document(uint32_t doc_id, std::string_view title, std::string_view text);

    /**
     * @brief Ingest a JSONL file containing {"id":..., "title":..., "text":...} documents.
     */
    size_t index_jsonl_file(const std::filesystem::path& jsonl_path);

    /**
     * @brief Build and write the final unified .idx file to disk.
     */
    void write_index(const std::filesystem::path& output_idx_path);
    void set_enable_fm_index(bool enable) noexcept { enable_fm_index_ = enable; }
    [[nodiscard]] bool is_fm_index_enabled() const noexcept { return enable_fm_index_; }

    [[nodiscard]] size_t total_docs() const noexcept { return doc_metadata_.size(); }
    [[nodiscard]] size_t total_terms() const noexcept { return term_postings_.size(); }

  private:
    void accumulate_document(uint32_t doc_id, std::string_view title, std::string_view text);

    bool enable_fm_index_{false};

    Analyzer analyzer_{true, true};

    // Stored fields buffer (raw text and titles)
    std::vector<uint8_t> stored_fields_{};
    std::vector<DocMetadataRecord> doc_metadata_{};

    // In-memory postings map: term -> vector of postings
    std::map<std::string, std::vector<DocPosting>> term_postings_{};

    uint64_t total_tokens_{0};
};

}  // namespace needlefish
