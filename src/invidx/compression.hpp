#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace needlefish {

/**
 * @brief Fast Variable-Byte (Varint) Integer Codec.
 * 7 bits of payload per byte, MSB = 1 indicates continuation.
 */
class Varint {
  public:
    static size_t encode_uint32(uint32_t val, uint8_t* out) noexcept;
    static size_t decode_uint32(const uint8_t* in, uint32_t* val) noexcept;
    static size_t decode_uint32(const uint8_t* in, const uint8_t* end, uint32_t* val) noexcept;

    static void encode_sequence(std::span<const uint32_t> values, std::vector<uint8_t>& out);
    static void decode_sequence(std::span<const uint8_t> in, size_t count,
                                std::vector<uint32_t>& out);
};

/**
 * @brief Frame-Of-Reference (FOR) Bit Packing / Unpacking for 128-integer blocks.
 * Supports bit widths 0..32 bits with portable and SIMD-accelerated decoders.
 */
class BitPacking {
  public:
    static constexpr size_t BLOCK_SIZE = 128;

    /**
     * @brief Computes the minimum bit-width needed to store all values in the block.
     */
    [[nodiscard]] static uint8_t required_bits(std::span<const uint32_t> block) noexcept;

    /**
     * @brief Pack 128 uint32 integers into bit_width bits per integer.
     * Output buffer requires exactly (128 * bit_width + 7) / 8 = 16 * bit_width bytes.
     */
    static void pack128(const uint32_t* in, uint8_t* out, uint8_t bit_width) noexcept;

    /**
     * @brief Portable scalar unpack 128 uint32 integers.
     */
    static void unpack128_scalar(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept;

    /**
     * @brief SIMD / vectorized unpack 128 uint32 integers.
     */
    static void unpack128_simd(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept;

    /**
     * @brief High-performance unpack choosing fastest available instruction set.
     */
    static void unpack128(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept;
};

}  // namespace needlefish
