#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "util/porter_stemmer.hpp"
#include "util/utf8.hpp"

namespace needlefish {

struct Token {
    std::string term{};
    uint32_t position{0};      // 0-indexed token sequence number
    uint32_t start_offset{0};  // Byte offset start in original text
    uint32_t end_offset{0};    // Byte offset end in original text

    bool operator==(const Token& other) const = default;
};

class Analyzer {
  public:
    Analyzer();
    explicit Analyzer(bool enable_stemming, bool filter_stopwords = true);

    void set_stopwords(std::unordered_set<std::string> stopwords);
    void add_stopword(std::string stopword);
    [[nodiscard]] bool is_stopword(std::string_view word) const noexcept;

    /**
     * @brief Analyze a text buffer into a sequence of normalized tokens.
     */
    [[nodiscard]] std::vector<Token> analyze(std::string_view text) const;

    /**
     * @brief Normalize and stem a single search query term.
     */
    [[nodiscard]] std::string normalize_term(std::string_view raw_term) const;

  private:
    bool enable_stemming_{true};
    bool filter_stopwords_{true};
    std::unordered_set<std::string> stopwords_{};
};

}  // namespace needlefish
