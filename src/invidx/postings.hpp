#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <vector>

#include "invidx/compression.hpp"

namespace needlefish {

struct DocPosting {
    uint32_t doc_id{0};
    uint32_t term_freq{0};
    std::vector<uint32_t> positions{};
};

struct PostingBlockHeader {
    uint32_t max_doc_id{0};
    float block_max_score{0.0f};
    uint16_t num_docs{0};
    uint8_t bit_width{0};
    uint32_t positions_offset{0};
    uint32_t positions_bytes{0};
};

/**
 * @brief Writer that compresses a stream of DocPosting into 128-doc blocks.
 */
class PostingListWriter {
  public:
    PostingListWriter() = default;

    void add_posting(uint32_t doc_id, uint32_t term_freq, std::span<const uint32_t> positions);
    void finish(std::vector<uint8_t>& postings_out, std::vector<uint8_t>& positions_out,
                std::span<const float> block_max_scores);

    [[nodiscard]] size_t doc_freq() const noexcept { return total_docs_; }
    [[nodiscard]] uint32_t max_doc_id() const noexcept { return max_doc_id_; }

  private:
    void flush_batch(size_t start_idx, size_t count, std::vector<uint8_t>& postings_out,
                     std::vector<uint8_t>& positions_out, float block_max_score);

    size_t total_docs_{0};
    uint32_t max_doc_id_{0};
    uint32_t last_doc_id_in_list_{0};

    std::vector<uint32_t> block_doc_ids_{};
    std::vector<uint32_t> block_freqs_{};
    std::vector<std::vector<uint32_t>> block_positions_{};
};

/**
 * @brief High-performance memory-mapped / span reader over compressed posting blocks.
 */
class PostingListReader {
  public:
    PostingListReader() = default;
    PostingListReader(std::span<const uint8_t> postings_data,
                      std::span<const uint8_t> positions_data, size_t total_docs);

    [[nodiscard]] bool valid() const noexcept { return !at_end_; }
    [[nodiscard]] uint32_t doc_id() const noexcept { return curr_doc_id_; }
    [[nodiscard]] uint32_t freq() const noexcept { return curr_freq_; }
    [[nodiscard]] float block_max_score() const noexcept {
        return curr_block_header_.block_max_score;
    }
    [[nodiscard]] uint32_t block_max_doc_id() const noexcept {
        return curr_block_header_.max_doc_id;
    }
    [[nodiscard]] size_t total_docs() const noexcept { return total_docs_; }

    /**
     * @brief Move to the next document in the posting list.
     */
    void next();

    /**
     * @brief Galloping/skipping advance to first doc >= target_doc_id.
     */
    void advance(uint32_t target_doc_id);

    /**
     * @brief Decode positions for the current document.
     */
    void read_positions(std::vector<uint32_t>& out) const;

  private:
    void load_next_block();

    std::span<const uint8_t> postings_data_{};
    std::span<const uint8_t> positions_data_{};
    size_t total_docs_{0};
    size_t docs_read_total_{0};

    size_t postings_offset_{0};
    PostingBlockHeader curr_block_header_{};

    uint32_t decoded_doc_ids_[128]{0};
    uint32_t decoded_freqs_[128]{0};
    uint32_t decoded_pos_offsets_[128]{0};
    uint32_t decoded_pos_lens_[128]{0};

    size_t block_idx_{0};
    uint32_t curr_doc_id_{0};
    uint32_t curr_freq_{0};
    bool at_end_{true};
};

}  // namespace needlefish
