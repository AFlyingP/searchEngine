#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <vector>

#if defined(__BMI2__)
#include <x86intrin.h>
#define NEEDLEFISH_HAS_BMI2 1
#endif

namespace needlefish {

/**
 * @brief Plain bitvector stored as a sequence of 64-bit unsigned integers.
 *
 * Provides O(1) bit access, modification, and basic bitwise operations.
 */
class BitVector {
  public:
    BitVector() = default;
    explicit BitVector(size_t num_bits, bool default_val = false);

    void resize(size_t num_bits, bool val = false);
    void push_back(bool bit);

    [[nodiscard]] size_t size() const noexcept { return num_bits_; }
    [[nodiscard]] bool empty() const noexcept { return num_bits_ == 0; }
    [[nodiscard]] size_t num_words() const noexcept { return words_.size(); }
    [[nodiscard]] const std::vector<uint64_t>& raw_words() const noexcept { return words_; }

    [[nodiscard]] bool get(size_t index) const noexcept {
        return (words_[index / 64] & (1ULL << (index % 64))) != 0;
    }

    [[nodiscard]] bool operator[](size_t index) const noexcept { return get(index); }

    void set(size_t index, bool val = true) noexcept {
        const size_t w = index / 64;
        const uint64_t mask = 1ULL << (index % 64);
        if (val) {
            words_[w] |= mask;
        } else {
            words_[w] &= ~mask;
        }
    }

    void clear() noexcept {
        words_.clear();
        num_bits_ = 0;
    }

    void serialize(std::ostream& os) const;
    static BitVector deserialize(std::istream& is);

  private:
    size_t num_bits_{0};
    std::vector<uint64_t> words_{};
};

/**
 * @brief Rank and Select directory structure over a BitVector.
 *
 * Superblocks are placed every 512 bits (8 uint64_t words).
 * Each superblock entry stores:
 *   - uint64_t cumulative 1-bit count up to the superblock start.
 *   - uint16_t relative 1-bit count for each of the 8 sub-blocks (words).
 *
 * Guarantees:
 *   - rank1(i) in O(1) time (< 5ns hot).
 *   - select1(k) in O(log(n/512)) + O(1) time.
 */
class RankSelectBitVector {
  public:
    struct DirectoryEntry {
        uint64_t superblock_rank{0};
        uint16_t subblock_ranks[8]{0, 0, 0, 0, 0, 0, 0, 0};
    };

    RankSelectBitVector() = default;
    explicit RankSelectBitVector(BitVector bv);

    /**
     * @brief Number of 1-bits in range [0, index).
     * @param index 0-indexed position in [0, size()].
     * @return Count of 1-bits before index.
     * Complexity: O(1)
     */
    [[nodiscard]] size_t rank1(size_t index) const noexcept;

    /**
     * @brief Number of 0-bits in range [0, index).
     * @param index 0-indexed position in [0, size()].
     * @return Count of 0-bits before index.
     * Complexity: O(1)
     */
    [[nodiscard]] size_t rank0(size_t index) const noexcept {
        if (index > size_) {
            index = size_;
        }
        return index - rank1(index);
    }

    /**
     * @brief Find the 0-indexed position of the k-th 1-bit (1-indexed k).
     * @param k 1-indexed target count (1 <= k <= total_ones()).
     * @return 0-indexed bit position in [0, size()).
     * Complexity: O(log(n / 512))
     */
    [[nodiscard]] size_t select1(size_t k) const noexcept;

    /**
     * @brief Find the 0-indexed position of the k-th 0-bit (1-indexed k).
     * @param k 1-indexed target count (1 <= k <= total_zeros()).
     * @return 0-indexed bit position in [0, size()).
     * Complexity: O(log(n / 512))
     */
    [[nodiscard]] size_t select0(size_t k) const noexcept;

    [[nodiscard]] bool get(size_t index) const noexcept { return bv_.get(index); }

    [[nodiscard]] bool operator[](size_t index) const noexcept { return bv_.get(index); }

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] size_t total_ones() const noexcept { return total_ones_; }
    [[nodiscard]] size_t total_zeros() const noexcept { return size_ - total_ones_; }
    [[nodiscard]] const BitVector& bitvector() const noexcept { return bv_; }

    void serialize(std::ostream& os) const;
    static RankSelectBitVector deserialize(std::istream& is);

  private:
    void build_directory();

    BitVector bv_{};
    size_t size_{0};
    size_t total_ones_{0};
    std::vector<DirectoryEntry> directory_{};
};

/**
 * @brief Portable & BMI2 select in 64-bit word.
 * Returns the 0-indexed position of the k-th 1-bit in word (1 <= k <= popcount(word)).
 */
[[nodiscard]] inline size_t select_in_word(uint64_t word, size_t k) noexcept {
#if defined(NEEDLEFISH_HAS_BMI2)
    // _pdep_u64 deposits the k-th bit (1ULL << (k-1)) into the corresponding 1-bit in word
    uint64_t deposited = _pdep_u64(1ULL << (k - 1), word);
    return static_cast<size_t>(std::countr_zero(deposited));
#else
    // Portable fallback: binary search on popcount
    size_t low = 0;
    size_t high = 64;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        uint64_t mask = (mid == 64) ? ~0ULL : ((1ULL << mid) - 1ULL);
        if (static_cast<size_t>(std::popcount(word & mask)) >= k) {
            high = mid;
        } else {
            low = mid + 1;
        }
    }
    return low - 1;
#endif
}

}  // namespace needlefish
