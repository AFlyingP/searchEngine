#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace needlefish {

/**
 * @brief Decodes a UTF-8 byte stream into 32-bit Unicode codepoints.
 * Replaces invalid byte sequences with U+FFFD. Never crashes or throws.
 */
class Utf8Decoder {
  public:
    static std::vector<char32_t> decode(std::string_view utf8_str);
    static std::string encode(char32_t codepoint);
    static std::string encode(const std::vector<char32_t>& codepoints);

    /**
     * @brief Check if codepoint is an alphanumeric word character.
     * Handles ASCII and Latin-1 supplement letters and digits.
     */
    [[nodiscard]] static bool is_word_char(char32_t cp) noexcept;

    /**
     * @brief Case-fold codepoint to lowercase (ASCII + Latin-1).
     */
    [[nodiscard]] static char32_t to_lower(char32_t cp) noexcept;

    /**
     * @brief Convert a UTF-8 string to lower-case UTF-8 string.
     */
    static std::string to_lower_utf8(std::string_view str);
};

}  // namespace needlefish
