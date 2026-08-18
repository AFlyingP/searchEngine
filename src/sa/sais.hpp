#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace needlefish {

struct SaisStats {
    size_t text_length{0};
    size_t peak_memory_bytes{0};
    size_t recursion_depth{0};
};

/**
 * @brief SA-IS (Nong, Zhang, Chan 2009) linear-time suffix array construction.
 * 
 * Constructs the Suffix Array of an integer or byte string in strictly O(n) time
 * and O(n) working space.
 * 
 * Suffixes are represented as 0-indexed positions in [0, n).
 * Note: Input string must have sentinel 0 at s[n-1], and all other characters s[i] > 0.
 */
template <typename IndexType>
class SaisBuilder {
public:
    static std::vector<IndexType> build(std::span<const IndexType> s, size_t alphabet_size,
                                       SaisStats* stats = nullptr);
};

/**
 * @brief Unified Suffix Array container holding either 32-bit or 64-bit indices.
 */
class SuffixArray {
public:
    SuffixArray() = default;
    explicit SuffixArray(std::vector<int32_t> sa32) : sa_(std::move(sa32)) {}
    explicit SuffixArray(std::vector<int64_t> sa64) : sa_(std::move(sa64)) {}

    [[nodiscard]] size_t size() const noexcept {
        return std::visit([](const auto& v) { return v.size(); }, sa_);
    }

    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }

    [[nodiscard]] bool is_64bit() const noexcept {
        return std::holds_alternative<std::vector<int64_t>>(sa_);
    }

    [[nodiscard]] size_t operator[](size_t index) const noexcept {
        return std::visit([index](const auto& v) -> size_t {
            return static_cast<size_t>(v[index]);
        }, sa_);
    }

    [[nodiscard]] const std::vector<int32_t>& as_32() const {
        return std::get<std::vector<int32_t>>(sa_);
    }

    [[nodiscard]] const std::vector<int64_t>& as_64() const {
        return std::get<std::vector<int64_t>>(sa_);
    }

private:
    std::variant<std::vector<int32_t>, std::vector<int64_t>> sa_{std::vector<int32_t>{}};
};

/**
 * @brief Build suffix array from raw byte text.
 * Appends 0-sentinel internally, maps characters to [1, 256], runs SA-IS in O(n),
 * and strips the leading sentinel from the resulting SA.
 * 
 * Automatically chooses 32-bit SA if n < 2^31, otherwise 64-bit SA.
 * 
 * Complexity: O(n) time, <= 6n bytes space in 32-bit mode.
 */
SuffixArray build_suffix_array(std::span<const uint8_t> text, SaisStats* stats = nullptr);
SuffixArray build_suffix_array(std::string_view text, SaisStats* stats = nullptr);

}  // namespace needlefish
