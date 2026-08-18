#include "util/utf8.hpp"

namespace needlefish {

namespace {

constexpr char32_t REPLACEMENT_CHAR = 0xFFFD;

}  // namespace

std::vector<char32_t> Utf8Decoder::decode(std::string_view utf8_str) {
    std::vector<char32_t> result;
    result.reserve(utf8_str.size());

    const auto* ptr = reinterpret_cast<const uint8_t*>(utf8_str.data());
    const size_t len = utf8_str.size();
    size_t i = 0;

    while (i < len) {
        const uint8_t b0 = ptr[i];
        if (b0 <= 0x7F) {
            // 1-byte ASCII
            result.push_back(static_cast<char32_t>(b0));
            i += 1;
        } else if ((b0 & 0xE0) == 0xC0) {
            // 2-byte sequence
            if (i + 1 >= len) {
                result.push_back(REPLACEMENT_CHAR);
                break;
            }
            const uint8_t b1 = ptr[i + 1];
            if ((b1 & 0xC0) != 0x80 || (b0 & 0x1E) == 0) {
                // Invalid or overlong
                result.push_back(REPLACEMENT_CHAR);
                i += 1;
                continue;
            }
            const char32_t cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);
            result.push_back(cp);
            i += 2;
        } else if ((b0 & 0xF0) == 0xE0) {
            // 3-byte sequence
            if (i + 2 >= len) {
                result.push_back(REPLACEMENT_CHAR);
                break;
            }
            const uint8_t b1 = ptr[i + 1];
            const uint8_t b2 = ptr[i + 2];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
                result.push_back(REPLACEMENT_CHAR);
                i += 1;
                continue;
            }
            const char32_t cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);
            // Check surrogates (0xD800 - 0xDFFF) and overlong (< 0x800)
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) {
                result.push_back(REPLACEMENT_CHAR);
            } else {
                result.push_back(cp);
            }
            i += 3;
        } else if ((b0 & 0xF8) == 0xF0) {
            // 4-byte sequence
            if (i + 3 >= len) {
                result.push_back(REPLACEMENT_CHAR);
                break;
            }
            const uint8_t b1 = ptr[i + 1];
            const uint8_t b2 = ptr[i + 2];
            const uint8_t b3 = ptr[i + 3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
                result.push_back(REPLACEMENT_CHAR);
                i += 1;
                continue;
            }
            const char32_t cp =
                ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            if (cp < 0x10000 || cp > 0x10FFFF) {
                result.push_back(REPLACEMENT_CHAR);
            } else {
                result.push_back(cp);
            }
            i += 4;
        } else {
            result.push_back(REPLACEMENT_CHAR);
            i += 1;
        }
    }

    return result;
}

std::string Utf8Decoder::encode(char32_t cp) {
    std::string out;
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        return encode(REPLACEMENT_CHAR);
    }
    return out;
}

std::string Utf8Decoder::encode(const std::vector<char32_t>& codepoints) {
    std::string out;
    out.reserve(codepoints.size());
    for (char32_t cp : codepoints) {
        out += encode(cp);
    }
    return out;
}

bool Utf8Decoder::is_word_char(char32_t cp) noexcept {
    // ASCII letters and digits
    if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') ||
        cp == '_') {
        return true;
    }
    // Latin-1 letters (À-Ö, Ø-ö, ø-ÿ)
    if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00F6) ||
        (cp >= 0x00F8 && cp <= 0x00FF)) {
        return true;
    }
    // General Unicode letter/digit ranges
    if ((cp >= 0x0100 && cp <= 0x024F) ||  // Latin Extended
        (cp >= 0x0370 && cp <= 0x03FF) ||  // Greek
        (cp >= 0x0400 && cp <= 0x04FF) ||  // Cyrillic
        (cp >= 0x4E00 && cp <= 0x9FFF)) {  // CJK Unified Ideographs
        return true;
    }
    return false;
}

char32_t Utf8Decoder::to_lower(char32_t cp) noexcept {
    // ASCII lowercase
    if (cp >= 'A' && cp <= 'Z') {
        return cp + ('a' - 'A');
    }
    // Latin-1 lowercase
    if ((cp >= 0x00C0 && cp <= 0x00D6) || (cp >= 0x00D8 && cp <= 0x00DE)) {
        return cp + 0x20;
    }
    return cp;
}

std::string Utf8Decoder::to_lower_utf8(std::string_view str) {
    auto cps = decode(str);
    for (auto& cp : cps) {
        cp = to_lower(cp);
    }
    return encode(cps);
}

}  // namespace needlefish
