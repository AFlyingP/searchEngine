#include "invidx/compression.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <stdexcept>

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

    while (bytes < 5) {
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

size_t Varint::decode_uint32(const uint8_t* in, const uint8_t* end, uint32_t* val) noexcept {
    if (in >= end) {
        *val = 0;
        return 0;
    }
    uint32_t result = 0;
    size_t shift = 0;
    size_t bytes = 0;

    while (bytes < 5 && in + bytes < end) {
        const uint8_t byte = in[bytes++];
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            *val = result;
            return bytes;
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
    const uint8_t* end = in.data() + in.size();
    for (size_t i = 0; i < count; ++i) {
        if (ptr >= end) {
            out[i] = 0;
            continue;
        }
        size_t b = decode_uint32(ptr, end, &out[i]);
        if (b == 0) {
            break;
        }
        ptr += b;
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
    alignas(8) uint64_t out_words[64] = {0};

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

    std::memcpy(out, out_words, num_words * sizeof(uint64_t));
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
    alignas(8) uint64_t in_words[64] = {0};
    std::memcpy(in_words, in, num_words * sizeof(uint64_t));

    const uint64_t mask = (bit_width == 64) ? ~0ULL : ((1ULL << bit_width) - 1ULL);

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
    if (bit_width == 0) {
        std::memset(out, 0, 128 * sizeof(uint32_t));
        return;
    }
    if (bit_width == 32) {
        std::memcpy(out, in, 128 * sizeof(uint32_t));
        return;
    }

#if defined(__x86_64__) || defined(_M_X64)
    if (bit_width == 8) {
        const __m128i zero = _mm_setzero_si128();
        for (size_t i = 0; i < 8; ++i) {
            __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i * 16));
            __m128i lo16 = _mm_unpacklo_epi8(bytes, zero);
            __m128i hi16 = _mm_unpackhi_epi8(bytes, zero);
            __m128i w0 = _mm_unpacklo_epi16(lo16, zero);
            __m128i w1 = _mm_unpackhi_epi16(lo16, zero);
            __m128i w2 = _mm_unpacklo_epi16(hi16, zero);
            __m128i w3 = _mm_unpackhi_epi16(hi16, zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i * 16), w0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i * 16 + 4), w1);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i * 16 + 8), w2);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i * 16 + 12), w3);
        }
        return;
    }

    if (bit_width == 16) {
        const __m128i zero = _mm_setzero_si128();
        for (size_t i = 0; i < 16; ++i) {
            __m128i shorts = _mm_loadu_si128(reinterpret_cast<const __m128i*>(in + i * 16));
            __m128i w0 = _mm_unpacklo_epi16(shorts, zero);
            __m128i w1 = _mm_unpackhi_epi16(shorts, zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i * 8), w0);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i * 8 + 4), w1);
        }
        return;
    }

    const size_t num_words = 2 * bit_width;
    alignas(8) uint64_t in_words[64] = {0};
    std::memcpy(in_words, in, num_words * sizeof(uint64_t));
    const uint64_t mask = (bit_width == 64) ? ~0ULL : ((1ULL << bit_width) - 1ULL);

    for (size_t i = 0; i < 128; i += 4) {
        for (size_t j = 0; j < 4; ++j) {
            const size_t idx = i + j;
            const size_t bit_pos = idx * bit_width;
            const size_t word_idx = bit_pos / 64;
            const size_t bit_offset = bit_pos % 64;
            uint64_t val = (in_words[word_idx] >> bit_offset);
            if (bit_offset + bit_width > 64 && word_idx + 1 < num_words) {
                val |= (in_words[word_idx + 1] << (64 - bit_offset));
            }
            out[idx] = static_cast<uint32_t>(val & mask);
        }
    }
    return;
#endif

    unpack128_scalar(in, out, bit_width);
}

void BitPacking::unpack128(const uint8_t* in, uint32_t* out, uint8_t bit_width) noexcept {
    unpack128_simd(in, out, bit_width);
}

}  // namespace needlefish
