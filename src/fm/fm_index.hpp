#pragma once

#include "bitvector/bitvector.hpp"
#include "sa/sais.hpp"
#include "wavelet/wavelet_tree.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace needlefish {

/**
 * @brief FM-Index (Burrows-Wheeler Transform + Wavelet Tree + Sampled Suffix Array).
 * 
 * Provides:
 *   - count(P): O(m) exact substring occurrence count.
 *   - locate(P): O(m + occ * s) exact occurrence position retrieval.
 *   - extract(begin, end): O(len + s) text snippet reconstruction.
 * 
 * Hand-implemented without external dependencies.
 */
class FMIndex {
public:
    static constexpr size_t DEFAULT_SAMPLE_RATE = 32;

    FMIndex() = default;

    /**
     * @brief Build FM-Index over text.
     * @param text Input text (must not contain sentinel 0 byte).
     * @param sample_rate Sampling rate s for Suffix Array (default = 32).
     */
    explicit FMIndex(std::span<const uint8_t> text, size_t sample_rate = DEFAULT_SAMPLE_RATE);
    explicit FMIndex(std::string_view text, size_t sample_rate = DEFAULT_SAMPLE_RATE);

    [[nodiscard]] size_t size() const noexcept { return text_size_; }
    [[nodiscard]] bool empty() const noexcept { return text_size_ == 0; }
    [[nodiscard]] size_t sample_rate() const noexcept { return sample_rate_; }

    /**
     * @brief Find occurrence range [lo, hi) in suffix array for pattern P.
     * Complexity: O(m) where m = pattern.size().
     */
    [[nodiscard]] std::pair<size_t, size_t> backward_search(
        std::span<const uint8_t> pattern) const noexcept;
    [[nodiscard]] std::pair<size_t, size_t> backward_search(
        std::string_view pattern) const noexcept;

    /**
     * @brief Count exact occurrences of pattern P in the text.
     * Complexity: O(m).
     */
    [[nodiscard]] size_t count(std::span<const uint8_t> pattern) const noexcept;
    [[nodiscard]] size_t count(std::string_view pattern) const noexcept;

    /**
     * @brief Locate all 0-indexed text positions where pattern P occurs.
     * Complexity: O(m + occ * sample_rate).
     */
    [[nodiscard]] std::vector<size_t> locate(std::span<const uint8_t> pattern) const;
    [[nodiscard]] std::vector<size_t> locate(std::string_view pattern) const;

    /**
     * @brief Extract text substring in range [begin, end).
     * Complexity: O((end - begin) + sample_rate).
     */
    [[nodiscard]] std::string extract(size_t begin, size_t end) const;

    /**
     * @brief LF-mapping for row r: LF(r) = C[L[r]] + rank_{L[r]}(r).
     */
    [[nodiscard]] size_t lf_map(size_t r) const noexcept {
        const uint8_t c = bwt_wt_.access(r);
        return c_table_[c] + bwt_wt_.rank(c, r);
    }

    void serialize(std::ostream& os) const;
    static FMIndex deserialize(std::istream& is);

private:
    void build(std::span<const uint8_t> text, size_t sample_rate);
    [[nodiscard]] size_t locate_row(size_t r) const noexcept;

    size_t text_size_{0};
    size_t bwt_size_{0};
    size_t sample_rate_{DEFAULT_SAMPLE_RATE};
    size_t primary_index_{0};  // Row of the sentinel in BWT

    std::array<size_t, 256> c_table_{};
    WaveletTree bwt_wt_{};

    // Sampled Suffix Array (by text position: SA[r] % sample_rate == 0)
    RankSelectBitVector sampled_rows_bv_{};
    std::vector<uint32_t> sampled_sa_32_{};
    std::vector<uint64_t> sampled_sa_64_{};
    bool is_64bit_{false};

    // Inverse sampled positions for snippet extract:
    // text_pos / sample_rate -> SA row
    std::vector<size_t> inv_sampled_rows_{};
};

}  // namespace needlefish
