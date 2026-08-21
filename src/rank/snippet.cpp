#include "rank/snippet.hpp"

#include <algorithm>
#include <span>
#include <unordered_set>

namespace needlefish {

namespace {

std::string html_escape(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            case '\'':
                out += "&#39;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

// Adjust byte offset so we don't slice in the middle of a multi-byte UTF-8 sequence
size_t adjust_utf8_boundary(std::string_view text, size_t pos) {
    if (pos >= text.size()) {
        return text.size();
    }
    // If text[pos] is a UTF-8 continuation byte (10xxxxxx), move backwards to the start of the sequence
    while (pos > 0 && (static_cast<uint8_t>(text[pos]) & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

}  // namespace

SnippetGenerator::SnippetGenerator(size_t max_snippet_len, std::string_view pre_tag,
                                   std::string_view post_tag)
    : max_snippet_len_(max_snippet_len), pre_tag_(pre_tag), post_tag_(post_tag) {}

std::string SnippetGenerator::highlight(std::string_view doc_text,
                                        std::span<const std::string> query_terms) const {
    if (doc_text.empty()) {
        return "";
    }

    if (query_terms.empty()) {
        const size_t len = adjust_utf8_boundary(doc_text, std::min(doc_text.size(), max_snippet_len_));
        std::string snippet = html_escape(doc_text.substr(0, len));
        if (doc_text.size() > max_snippet_len_) {
            snippet += "...";
        }
        return snippet;
    }

    std::unordered_set<std::string> target_terms;
    for (const auto& qt : query_terms) {
        target_terms.insert(qt);
        target_terms.insert(analyzer_.normalize_term(qt));
    }

    // Tokenize full text
    auto tokens = analyzer_.analyze(doc_text);
    if (tokens.empty()) {
        const size_t len = adjust_utf8_boundary(doc_text, std::min(doc_text.size(), max_snippet_len_));
        return html_escape(doc_text.substr(0, len));
    }

    // Find token with highest local density of matching query terms
    size_t best_tok_idx = 0;
    int best_score = -1;

    for (size_t i = 0; i < tokens.size(); ++i) {
        int score = 0;
        const size_t start_byte = tokens[i].start_offset;
        for (size_t j = i; j < tokens.size(); ++j) {
            if (tokens[j].end_offset - start_byte > max_snippet_len_) {
                break;
            }
            if (target_terms.find(tokens[j].term) != target_terms.end()) {
                score += 10;
            }
        }
        if (score > best_score) {
            best_score = score;
            best_tok_idx = i;
        }
    }

    const size_t window_start = tokens[best_tok_idx].start_offset;
    size_t window_end = std::min(doc_text.size(), window_start + max_snippet_len_);

    // Adjust window boundaries to whitespace if possible
    if (window_end < doc_text.size()) {
        size_t next_space = doc_text.find_first_of(" \t\n\r.,!?", window_end);
        if (next_space != std::string_view::npos &&
            next_space - window_start <= max_snippet_len_ + 20) {
            window_end = next_space;
        }
    }
    window_end = adjust_utf8_boundary(doc_text, window_end);

    // Build highlighted string inside window
    std::string result;
    if (window_start > 0) {
        result += "...";
    }

    size_t curr_pos = window_start;
    for (const auto& tok : tokens) {
        if (tok.end_offset <= window_start) {
            continue;
        }
        if (tok.start_offset >= window_end) {
            break;
        }

        if (tok.start_offset > curr_pos) {
            result += html_escape(doc_text.substr(curr_pos, tok.start_offset - curr_pos));
        }

        std::string term_slice = html_escape(doc_text.substr(tok.start_offset, tok.end_offset - tok.start_offset));
        if (target_terms.find(tok.term) != target_terms.end()) {
            result += pre_tag_;
            result += term_slice;
            result += post_tag_;
        } else {
            result += term_slice;
        }

        curr_pos = tok.end_offset;
    }

    if (curr_pos < window_end) {
        result += html_escape(doc_text.substr(curr_pos, window_end - curr_pos));
    }

    if (window_end < doc_text.size()) {
        result += "...";
    }

    return result;
}

}  // namespace needlefish
