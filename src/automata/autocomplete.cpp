#include "automata/autocomplete.hpp"

#include <algorithm>
#include <sstream>

namespace needlefish {

AutocompleteEngine::AutocompleteEngine(const IndexView& index) : index_(index) {}

std::vector<Suggestion> AutocompleteEngine::prefix_suggest(std::string_view prefix,
                                                           size_t max_results) const {
    std::vector<Suggestion> suggestions;
    if (prefix.empty()) {
        return suggestions;
    }

    std::string lowered = Utf8Decoder::to_lower_utf8(prefix);
    auto raw_matches = index_.term_dict().prefix_search(lowered, max_results * 2);

    for (const auto& [term, payload] : raw_matches) {
        suggestions.push_back(Suggestion{.text = term,
                                         .doc_freq = payload.doc_freq,
                                         .edit_distance = 0,
                                         .score = static_cast<float>(payload.doc_freq)});
    }

    std::sort(suggestions.begin(), suggestions.end(), [](const Suggestion& a, const Suggestion& b) {
        if (a.doc_freq != b.doc_freq)
            return a.doc_freq > b.doc_freq;
        return a.text < b.text;
    });

    if (suggestions.size() > max_results) {
        suggestions.resize(max_results);
    }
    return suggestions;
}

std::vector<Suggestion> AutocompleteEngine::fuzzy_suggest(std::string_view word,
                                                          size_t max_distance,
                                                          size_t max_results) const {
    std::vector<Suggestion> suggestions;
    if (word.empty()) {
        return suggestions;
    }

    std::string lowered = Utf8Decoder::to_lower_utf8(word);
    LevenshteinAutomaton dfa(lowered, max_distance);
    auto matches = dfa.match_trie(index_.term_dict(), max_results * 2);

    for (const auto& m : matches) {
        suggestions.push_back(Suggestion{.text = m.term,
                                         .doc_freq = m.payload.doc_freq,
                                         .edit_distance = m.distance,
                                         .score = (1.0f / (static_cast<float>(m.distance) + 1.0f)) *
                                                  static_cast<float>(m.payload.doc_freq)});
    }

    std::sort(suggestions.begin(), suggestions.end(), [](const Suggestion& a, const Suggestion& b) {
        if (a.edit_distance != b.edit_distance)
            return a.edit_distance < b.edit_distance;
        if (a.doc_freq != b.doc_freq)
            return a.doc_freq > b.doc_freq;
        return a.text < b.text;
    });

    if (suggestions.size() > max_results) {
        suggestions.resize(max_results);
    }
    return suggestions;
}

std::string AutocompleteEngine::did_you_mean(std::string_view query) const {
    auto tokens = analyzer_.analyze(query);
    if (tokens.empty()) {
        return std::string(query);
    }

    std::stringstream corrected;
    bool any_correction = false;

    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0)
            corrected << " ";

        const std::string& term = tokens[i].term;
        auto direct = index_.term_dict().lookup(term);
        if (direct.valid()) {
            corrected << term;
        } else {
            auto suggestions = fuzzy_suggest(term, 2, 1);
            if (!suggestions.empty() && suggestions[0].text != term) {
                corrected << suggestions[0].text;
                any_correction = true;
            } else {
                corrected << term;
            }
        }
    }

    if (!any_correction) {
        return "";
    }
    return corrected.str();
}

}  // namespace needlefish
