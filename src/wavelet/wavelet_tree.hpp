#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <span>
#include <string_view>
#include <vector>

#include "bitvector/bitvector.hpp"

namespace needlefish {

/**
 * @brief 8-level bit-sliced Wavelet Tree over byte alphabet (0..255).
 *
 * Supports:
 *   - access(i): O(1) time (8 rank calls).
 *   - rank(c, i): O(1) time (8 rank calls).
 *   - select(c, k): O(1) time (8 select calls).
 */
class WaveletTree {
  public:
    WaveletTree() = default;
    explicit WaveletTree(std::span<const uint8_t> text);
    explicit WaveletTree(std::string_view text);

    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

    [[nodiscard]] uint8_t access(size_t index) const noexcept;
    [[nodiscard]] size_t rank(uint8_t c, size_t index) const noexcept;
    [[nodiscard]] size_t select(uint8_t c, size_t k) const noexcept;

    [[nodiscard]] size_t count(uint8_t c) const noexcept { return counts_[c]; }

    [[nodiscard]] const std::array<size_t, 256>& c_table() const noexcept { return c_table_; }

    void serialize(std::ostream& os) const;
    static WaveletTree deserialize(std::istream& is);

  private:
    void build(std::span<const uint8_t> text);
    [[nodiscard]] size_t prefix_count(size_t level, size_t node_prefix) const noexcept;

    size_t size_{0};
    std::array<size_t, 256> counts_{};
    std::array<size_t, 256> c_table_{};
    std::array<RankSelectBitVector, 8> levels_{};

    // Precomputed prefix offsets for level nodes:
    // node_offsets_[level][node_idx] is the start index of node_idx at level
    std::vector<std::vector<size_t>> node_offsets_{};
};

}  // namespace needlefish
