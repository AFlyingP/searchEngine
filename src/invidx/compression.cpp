#include "invidx/compression.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define NEEDLEFISH_HAS_X86_SIMD 1
#endif

namespace needlefish {

size_t Varint::encode_uint32(uint32_t val, uint8_t* out) noexcept {
    size_t bytes = 0;
    while (val >= 0x80) {
        out[bytes++] = static_cast<uint8_t>((val & 0x7F) | 0x80);
        val >>= 7;
    }
    out[bytes++] = static_cast<uint8_t>(val & 0x7F);
    return bytes;
}

size_t Varint::decode_uint32(const uint8_t* in, uint32_t* val) noexcept {
    uint32_t result = 0;
    size_t shift = 0;
    size_t bytes = 0;

    while (true) {
        const uint8_t byte = in[bytes++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
    }

    *val = result;
    return bytes;
}

void Varint::encode_sequence(std::span<const uint32_t> values, std::vector<uint8_t>& out) {
    uint8_t buf[5];
    for (uint32_t val : values) {
        const size_t len = encode_uint32(val, buf);
        out.insert(out.end(), buf, buf + len);
    }
}

void Varint::decode_sequence(std::span<const uint8_t> in, size_t count,
                             std::vector<uint32_t>& out) {
    out.resize(count);
    const uint8_t* ptr = in.data();
    for (size_t i = 0; i < count; ++i) {
        ptr += decode_uint32(ptr, &out[i]);
    }
}

uint8_t BitPacking::required_bits(std::span<const uint32_t> block) noexcept {
    uint32_t max_val = 0;
    for (uint32_t v : block) {
        if (v > max_val) {
            max_val = v;
        }
    }
    if (max_val == 0) {
        return 0;
    }
    return static_cast<uint8_t>(32 - std::countl_zero(max_val));
}

void BitPacking::pack128(const uint32_t* in, uint8_t* out, uint8_t bit_width) noexcept {
    if (bit_width == 0) {
        return;
    }
    if (bit_width == 32) {
        std::memcpy(out, in, 128 * sizeof(uint32_t));
        return;
    }

    const size_t num_words = 2 * bit_width;
    auto* out_words = reinterpret_cast<uint64_t*>(out);
    std::memset(out_words, 0, num_words * sizeof(uint64_t));

    for (size_t i = 0; i < 128; ++i) {
        const uint64_t val = in[i];
        const size_t bit_pos = i * bit_width;
        const size_t word_idx = bit_pos / 64;
        const size_t bit_offset = bit_pos % 64;

        out_words[word_idx] |= (val << bit_offset);
        if (bit_offset + bit_width > 64 && word_idx + 1 < num_words) {
            out_words[word_idx + 1] |= (val >> (64 - bit_offset));
        }
    }
}

void BitPacking::unpack128_scalar(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept {
    if (bit_width == 0) {
        std::memset(out, 0, 128 * sizeof(uint32_t));
        return;
    }
    if (bit_width == 32) {
        std::memcpy(out, in, 128 * sizeof(uint32_t));
        return;
    }

    const size_t num_words = 2 * bit_width;
    const auto* in_words = reinterpret_cast<const uint64_t*>(in);
    const uint64_t mask = (1ULL << bit_width) - 1ULL;

    for (size_t i = 0; i < 128; ++i) {
        const size_t bit_pos = i * bit_width;
        const size_t word_idx = bit_pos / 64;
        const size_t bit_offset = bit_pos % 64;

        uint64_t val = (in_words[word_idx] >> bit_offset);
        if (bit_offset + bit_width > 64 && word_idx + 1 < num_words) {
            val |= (in_words[word_idx + 1] << (64 - bit_offset));
        }
        out[i] = static_cast<uint32_t>(val & mask);
    }
}

void BitPacking::unpack128_simd(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept {
    unpack128_scalar(in, out, bit_width);
}

void BitPacking::unpack128(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept {
    unpack128_simd(in, out, bit_width);
}

}  // namespace needlefish
