#include "automata/levenshtein.hpp"

#include <algorithm>

namespace needlefish {

LevenshteinAutomaton::LevenshteinAutomaton(std::string_view target, size_t max_distance)
    : target_(target), max_distance_(max_distance) {}

LevenshteinAutomaton::State LevenshteinAutomaton::initial_state() const {
    State state(target_.size() + 1);
    for (size_t i = 0; i <= target_.size(); ++i) {
        state[i] = static_cast<uint8_t>(i);
    }
    return state;
}

LevenshteinAutomaton::State LevenshteinAutomaton::step(const State& state, char c) const {
    const size_t m = target_.size();
    State next_state(m + 1);

    next_state[0] = static_cast<uint8_t>(state[0] + 1);

    for (size_t j = 1; j <= m; ++j) {
        const uint8_t cost = (target_[j - 1] == c) ? 0 : 1;
        uint8_t sub = static_cast<uint8_t>(state[j - 1] + cost);
        uint8_t ins = static_cast<uint8_t>(next_state[j - 1] + 1);
        uint8_t del = static_cast<uint8_t>(state[j] + 1);

        next_state[j] = std::min({sub, ins, del});
    }

    return next_state;
}

bool LevenshteinAutomaton::is_accept(const State& state) const noexcept {
    if (state.empty())
        return false;
    return state.back() <= max_distance_;
}

bool LevenshteinAutomaton::can_match(const State& state) const noexcept {
    if (state.empty())
        return false;
    uint8_t min_d = state[0];
    for (uint8_t d : state) {
        if (d < min_d)
            min_d = d;
    }
    return min_d <= max_distance_;
}

size_t LevenshteinAutomaton::distance(const State& state) const noexcept {
    if (state.empty())
        return max_distance_ + 1;
    return state.back();
}

void LevenshteinAutomaton::dfs_trie(uint32_t node_idx, const State& state,
                                    std::string& current_prefix, const RadixTrie& trie,
                                    std::vector<FuzzyMatch>& matches, size_t max_results) const {
    if (matches.size() >= max_results || node_idx >= trie.nodes().size() || !can_match(state)) {
        return;
    }

    const auto& node = trie.nodes()[node_idx];

    if ((node.flags & 1) != 0 && is_accept(state)) {
        matches.push_back(FuzzyMatch{
            .term = current_prefix, .distance = distance(state), .payload = node.payload});
    }

    uint32_t child_idx = node.first_child;
    while (child_idx != 0 && matches.size() < max_results) {
        const auto& child = trie.nodes()[child_idx];
        std::string_view edge(trie.string_pool().data() + child.edge_offset, child.edge_len);

        State curr_state = state;
        bool pruned = false;

        for (char c : edge) {
            curr_state = step(curr_state, c);
            if (!can_match(curr_state)) {
                pruned = true;
                break;
            }
        }

        if (!pruned) {
            const size_t prev_len = current_prefix.size();
            current_prefix.append(edge);
            dfs_trie(child_idx, curr_state, current_prefix, trie, matches, max_results);
            current_prefix.resize(prev_len);
        }

        child_idx = child.next_sibling;
    }
}

std::vector<FuzzyMatch> LevenshteinAutomaton::match_trie(const RadixTrie& trie,
                                                         size_t max_results) const {
    std::vector<FuzzyMatch> matches;
    if (trie.nodes().empty()) {
        return matches;
    }

    std::string current_prefix;
    State start_state = initial_state();
    dfs_trie(0, start_state, current_prefix, trie, matches, max_results);

    // Sort matches: closest distance first, then higher doc_freq
    std::sort(matches.begin(), matches.end(), [](const FuzzyMatch& a, const FuzzyMatch& b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        return a.payload.doc_freq > b.payload.doc_freq;
    });

    if (matches.size() > max_results) {
        matches.resize(max_results);
    }
    return matches;
}

}  // namespace needlefish
