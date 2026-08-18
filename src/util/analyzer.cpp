#include "util/analyzer.hpp"

namespace needlefish {

namespace {

const std::unordered_set<std::string> DEFAULT_STOPWORDS = {
    "a",      "about",    "above",     "after",      "again",   "against", "all",     "am",
    "an",     "and",      "any",       "are",        "as",      "at",      "be",      "because",
    "been",   "before",   "being",     "below",      "between", "both",    "but",     "by",
    "could",  "did",      "do",        "does",       "doing",   "down",    "during",  "each",
    "few",    "for",      "from",      "further",    "had",     "has",     "have",    "having",
    "he",     "her",      "here",      "hers",       "herself", "him",     "himself", "his",
    "how",    "i",        "if",        "in",         "into",    "is",      "it",      "its",
    "itself", "me",       "more",      "most",       "my",      "myself",  "no",      "nor",
    "not",    "of",       "off",       "on",         "once",    "only",    "or",      "other",
    "ought",  "our",      "ours",      "ourselves",  "out",     "over",    "own",     "same",
    "she",    "should",   "so",        "some",       "such",    "than",    "that",    "the",
    "their",  "theirs",   "them",      "themselves", "then",    "there",   "these",   "they",
    "this",   "those",    "through",   "to",         "too",     "under",   "until",   "up",
    "very",   "was",      "we",        "were",       "what",    "when",    "where",   "which",
    "while",  "who",      "whom",      "why",        "with",    "would",   "you",     "your",
    "yours",  "yourself", "yourselves"};

}  // namespace

Analyzer::Analyzer() : Analyzer(true, true) {}

Analyzer::Analyzer(bool enable_stemming, bool filter_stopwords)
    : enable_stemming_(enable_stemming)
    , filter_stopwords_(filter_stopwords)
    , stopwords_(DEFAULT_STOPWORDS) {}

void Analyzer::set_stopwords(std::unordered_set<std::string> stopwords) {
    stopwords_ = std::move(stopwords);
}

void Analyzer::add_stopword(std::string stopword) {
    stopwords_.insert(std::move(stopword));
}

bool Analyzer::is_stopword(std::string_view word) const noexcept {
    return stopwords_.find(std::string(word)) != stopwords_.end();
}

std::string Analyzer::normalize_term(std::string_view raw_term) const {
    std::string lowered = Utf8Decoder::to_lower_utf8(raw_term);
    if (enable_stemming_) {
        return PorterStemmer::stem(lowered);
    }
    return lowered;
}

std::vector<Token> Analyzer::analyze(std::string_view text) const {
    std::vector<Token> tokens;
    if (text.empty()) {
        return tokens;
    }

    const auto* ptr = reinterpret_cast<const uint8_t*>(text.data());
    const size_t len = text.size();
    size_t i = 0;
    uint32_t pos = 0;

    while (i < len) {
        // Skip non-word characters
        while (i < len) {
            size_t char_len = 1;
            char32_t cp = ptr[i];
            if ((ptr[i] & 0x80) != 0) {
                // Multi-byte UTF-8 character length
                if ((ptr[i] & 0xE0) == 0xC0)
                    char_len = 2;
                else if ((ptr[i] & 0xF0) == 0xE0)
                    char_len = 3;
                else if ((ptr[i] & 0xF8) == 0xF0)
                    char_len = 4;
                if (i + char_len > len)
                    char_len = len - i;
                auto decoded = Utf8Decoder::decode(std::string_view(text.data() + i, char_len));
                if (!decoded.empty())
                    cp = decoded[0];
            }
            if (Utf8Decoder::is_word_char(cp)) {
                break;
            }
            i += char_len;
        }

        if (i >= len) {
            break;
        }

        const size_t token_start = i;

        // Consume word characters
        while (i < len) {
            size_t char_len = 1;
            char32_t cp = ptr[i];
            if ((ptr[i] & 0x80) != 0) {
                if ((ptr[i] & 0xE0) == 0xC0)
                    char_len = 2;
                else if ((ptr[i] & 0xF0) == 0xE0)
                    char_len = 3;
                else if ((ptr[i] & 0xF8) == 0xF0)
                    char_len = 4;
                if (i + char_len > len)
                    char_len = len - i;
                auto decoded = Utf8Decoder::decode(std::string_view(text.data() + i, char_len));
                if (!decoded.empty())
                    cp = decoded[0];
            }
            if (!Utf8Decoder::is_word_char(cp)) {
                break;
            }
            i += char_len;
        }

        const size_t token_end = i;
        std::string_view raw_word(text.data() + token_start, token_end - token_start);

        std::string lowered = Utf8Decoder::to_lower_utf8(raw_word);
        if (filter_stopwords_ && is_stopword(lowered)) {
            continue;
        }

        std::string term = enable_stemming_ ? PorterStemmer::stem(lowered) : lowered;
        tokens.push_back(Token{.term = std::move(term),
                               .position = pos++,
                               .start_offset = static_cast<uint32_t>(token_start),
                               .end_offset = static_cast<uint32_t>(token_end)});
    }

    return tokens;
}

}  // namespace needlefish
