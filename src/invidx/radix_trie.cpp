#include "invidx/radix_trie.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace needlefish {

RadixTrie::RadixTrie() {
    // Node 0 is always the root node
    nodes_.push_back(RadixNode{.edge_offset = 0,
                               .edge_len = 0,
                               .flags = 0,
                               .first_child = 0,
                               .next_sibling = 0,
                               .payload = TermPayload{}});
}

void RadixTrie::insert(std::string_view key, const TermPayload& payload) {
    if (key.empty()) {
        nodes_[0].flags |= 1;
        nodes_[0].payload = payload;
        num_terms_++;
        return;
    }

    uint32_t curr_idx = 0;
    std::string_view remaining = key;

    while (!remaining.empty()) {
        uint32_t child_idx = nodes_[curr_idx].first_child;
        uint32_t prev_child_idx = 0;
        uint32_t matched_child = 0;
        size_t common_len = 0;

        while (child_idx != 0) {
            std::string_view edge(pool_.data() + nodes_[child_idx].edge_offset,
                                  nodes_[child_idx].edge_len);
            size_t l = 0;
            while (l < edge.size() && l < remaining.size() && edge[l] == remaining[l]) {
                l++;
            }
            if (l > 0) {
                matched_child = child_idx;
                common_len = l;
                break;
            }
            prev_child_idx = child_idx;
            child_idx = nodes_[child_idx].next_sibling;
        }

        if (matched_child == 0) {
            // No matching child, create new node
            const uint32_t new_offset = static_cast<uint32_t>(pool_.size());
            pool_.append(remaining);

            const uint32_t new_idx = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back(RadixNode{.edge_offset = new_offset,
                                       .edge_len = static_cast<uint16_t>(remaining.size()),
                                       .flags = 1,
                                       .first_child = 0,
                                       .next_sibling = nodes_[curr_idx].first_child,
                                       .payload = payload});
            nodes_[curr_idx].first_child = new_idx;
            num_terms_++;
            return;
        }

        const size_t edge_len = nodes_[matched_child].edge_len;

        if (common_len == edge_len) {
            // Full edge match
            remaining.remove_prefix(common_len);
            if (remaining.empty()) {
                if ((nodes_[matched_child].flags & 1) == 0) {
                    num_terms_++;
                }
                nodes_[matched_child].flags |= 1;
                nodes_[matched_child].payload = payload;
                return;
            }
            curr_idx = matched_child;
        } else {
            // Partial match: split matched_child
            const uint32_t split_edge_offset = nodes_[matched_child].edge_offset;
            const uint32_t child_new_offset = split_edge_offset + static_cast<uint32_t>(common_len);
            const uint16_t child_new_len = static_cast<uint16_t>(edge_len - common_len);

            // Update matched_child to represent remainder
            nodes_[matched_child].edge_offset = child_new_offset;
            nodes_[matched_child].edge_len = child_new_len;

            // Create split node
            const uint32_t split_idx = static_cast<uint32_t>(nodes_.size());
            RadixNode split_node{.edge_offset = split_edge_offset,
                                 .edge_len = static_cast<uint16_t>(common_len),
                                 .flags = 0,
                                 .first_child = matched_child,
                                 .next_sibling = nodes_[matched_child].next_sibling,
                                 .payload = TermPayload{}};
            nodes_[matched_child].next_sibling = 0;
            nodes_.push_back(split_node);

            // Link split_node into parent/sibling list
            if (prev_child_idx == 0) {
                nodes_[curr_idx].first_child = split_idx;
            } else {
                nodes_[prev_child_idx].next_sibling = split_idx;
            }

            remaining.remove_prefix(common_len);
            if (remaining.empty()) {
                nodes_[split_idx].flags |= 1;
                nodes_[split_idx].payload = payload;
                num_terms_++;
                return;
            }

            // Create new child of split_idx
            const uint32_t new_offset = static_cast<uint32_t>(pool_.size());
            pool_.append(remaining);

            const uint32_t new_leaf_idx = static_cast<uint32_t>(nodes_.size());
            nodes_.push_back(RadixNode{.edge_offset = new_offset,
                                       .edge_len = static_cast<uint16_t>(remaining.size()),
                                       .flags = 1,
                                       .first_child = 0,
                                       .next_sibling = nodes_[split_idx].first_child,
                                       .payload = payload});
            nodes_[split_idx].first_child = new_leaf_idx;
            num_terms_++;
            return;
        }
    }
}

TermPayload RadixTrie::lookup(std::string_view key) const noexcept {
    if (nodes_.empty()) {
        return TermPayload{};
    }
    if (key.empty()) {
        if ((nodes_[0].flags & 1) != 0) {
            return nodes_[0].payload;
        }
        return TermPayload{};
    }

    uint32_t curr_idx = 0;
    std::string_view remaining = key;

    while (!remaining.empty()) {
        uint32_t child_idx = nodes_[curr_idx].first_child;
        bool found = false;

        while (child_idx != 0) {
            std::string_view edge(pool_.data() + nodes_[child_idx].edge_offset,
                                  nodes_[child_idx].edge_len);
            if (remaining.starts_with(edge)) {
                remaining.remove_prefix(edge.size());
                curr_idx = child_idx;
                found = true;
                break;
            }
            child_idx = nodes_[child_idx].next_sibling;
        }

        if (!found) {
            return TermPayload{};
        }
    }

    if ((nodes_[curr_idx].flags & 1) != 0) {
        return nodes_[curr_idx].payload;
    }
    return TermPayload{};
}

void RadixTrie::collect_subtree(uint32_t node_idx, std::string& current_prefix,
                                std::vector<std::pair<std::string, TermPayload>>& out,
                                size_t max_results) const {
    if (out.size() >= max_results || (node_idx == 0 && nodes_.empty())) {
        return;
    }

    if ((nodes_[node_idx].flags & 1) != 0) {
        out.emplace_back(current_prefix, nodes_[node_idx].payload);
    }

    uint32_t child_idx = nodes_[node_idx].first_child;
    while (child_idx != 0 && out.size() < max_results) {
        std::string_view edge(pool_.data() + nodes_[child_idx].edge_offset,
                              nodes_[child_idx].edge_len);
        const size_t prev_len = current_prefix.size();
        current_prefix.append(edge);

        collect_subtree(child_idx, current_prefix, out, max_results);

        current_prefix.resize(prev_len);
        child_idx = nodes_[child_idx].next_sibling;
    }
}

std::vector<std::pair<std::string, TermPayload>> RadixTrie::prefix_search(
    std::string_view prefix, size_t max_results) const {
    std::vector<std::pair<std::string, TermPayload>> results;
    if (nodes_.empty()) {
        return results;
    }

    uint32_t curr_idx = 0;
    std::string_view remaining = prefix;
    std::string matched_prefix;

    while (!remaining.empty()) {
        uint32_t child_idx = nodes_[curr_idx].first_child;
        bool found = false;

        while (child_idx != 0) {
            std::string_view edge(pool_.data() + nodes_[child_idx].edge_offset,
                                  nodes_[child_idx].edge_len);
            if (remaining.starts_with(edge)) {
                matched_prefix.append(edge);
                remaining.remove_prefix(edge.size());
                curr_idx = child_idx;
                found = true;
                break;
            } else if (edge.starts_with(remaining)) {
                // Prefix ends in the middle of this edge
                matched_prefix.append(edge);
                curr_idx = child_idx;
                found = true;
                remaining = "";
                break;
            }
            child_idx = nodes_[child_idx].next_sibling;
        }

        if (!found) {
            return results;
        }
    }

    collect_subtree(curr_idx, matched_prefix, results, max_results);
    return results;
}

void RadixTrie::dfa_dfs(
    uint32_t node_idx, int state, std::string& current_prefix,
    const std::function<int(int state, char c)>& step_fn,
    const std::function<bool(int state)>& is_accept_fn,
    const std::function<bool(int state)>& can_match_fn,
    const std::function<bool(std::string_view term, const TermPayload& payload)>& visitor) const {
    if (node_idx >= nodes_.size() || !can_match_fn(state)) {
        return;
    }

    if ((nodes_[node_idx].flags & 1) != 0 && is_accept_fn(state)) {
        if (!visitor(current_prefix, nodes_[node_idx].payload)) {
            return;
        }
    }

    uint32_t child_idx = nodes_[node_idx].first_child;
    while (child_idx != 0) {
        std::string_view edge(pool_.data() + nodes_[child_idx].edge_offset,
                              nodes_[child_idx].edge_len);
        int next_state = state;
        bool dead = false;

        for (char c : edge) {
            next_state = step_fn(next_state, c);
            if (!can_match_fn(next_state)) {
                dead = true;
                break;
            }
        }

        if (!dead) {
            const size_t prev_len = current_prefix.size();
            current_prefix.append(edge);
            dfa_dfs(child_idx, next_state, current_prefix, step_fn, is_accept_fn, can_match_fn,
                    visitor);
            current_prefix.resize(prev_len);
        }

        child_idx = nodes_[child_idx].next_sibling;
    }
}

void RadixTrie::traverse_with_dfa(
    const std::function<int(int state, char c)>& step_fn,
    const std::function<bool(int state)>& is_accept_fn,
    const std::function<bool(int state)>& can_match_fn,
    const std::function<bool(std::string_view term, const TermPayload& payload)>& visitor) const {
    if (nodes_.empty()) {
        return;
    }
    std::string prefix;
    dfa_dfs(0, 0, prefix, step_fn, is_accept_fn, can_match_fn, visitor);
}

void RadixTrie::serialize(std::ostream& os) const {
    const uint64_t num_n = static_cast<uint64_t>(nodes_.size());
    const uint64_t pool_sz = static_cast<uint64_t>(pool_.size());
    const uint64_t num_t = static_cast<uint64_t>(num_terms_);

    os.write(reinterpret_cast<const char*>(&num_n), sizeof(num_n));
    os.write(reinterpret_cast<const char*>(&pool_sz), sizeof(pool_sz));
    os.write(reinterpret_cast<const char*>(&num_t), sizeof(num_t));

    if (num_n > 0) {
        os.write(reinterpret_cast<const char*>(nodes_.data()),
                 static_cast<std::streamsize>(num_n * sizeof(RadixNode)));
    }
    if (pool_sz > 0) {
        os.write(pool_.data(), static_cast<std::streamsize>(pool_sz));
    }
}

RadixTrie RadixTrie::deserialize(std::istream& is) {
    RadixTrie trie;
    uint64_t num_n = 0;
    uint64_t pool_sz = 0;
    uint64_t num_t = 0;

    if (!is.read(reinterpret_cast<char*>(&num_n), sizeof(num_n)) ||
        !is.read(reinterpret_cast<char*>(&pool_sz), sizeof(pool_sz)) ||
        !is.read(reinterpret_cast<char*>(&num_t), sizeof(num_t))) {
        throw std::runtime_error("Failed to read RadixTrie header");
    }

    if (num_n == 0) {
        throw std::runtime_error("Corrupted RadixTrie: trie contains 0 nodes (rootless)");
    }

    constexpr uint64_t MAX_RADIX_NODES = 50'000'000ULL;
    constexpr uint64_t MAX_RADIX_POOL = 1024ULL * 1024ULL * 1024ULL; // 1 GB
    if (num_n > MAX_RADIX_NODES || pool_sz > MAX_RADIX_POOL) {
        throw std::runtime_error("Corrupted RadixTrie: node count or pool size exceeds maximum limits");
    }

    // Validate available stream bytes before allocation
    auto cur_pos = is.tellg();
    if (cur_pos != std::streampos(-1)) {
        is.seekg(0, std::ios::end);
        auto end_pos = is.tellg();
        is.seekg(cur_pos);
        if (end_pos >= cur_pos) {
            uint64_t rem_bytes = static_cast<uint64_t>(end_pos - cur_pos);
            if (num_n * sizeof(RadixNode) + pool_sz > rem_bytes) {
                throw std::runtime_error("Corrupted RadixTrie: required bytes exceed stream size");
            }
        }
    }

    trie.num_terms_ = static_cast<size_t>(num_t);
    trie.nodes_.resize(static_cast<size_t>(num_n));
    if (!is.read(reinterpret_cast<char*>(trie.nodes_.data()),
                 static_cast<std::streamsize>(num_n * sizeof(RadixNode)))) {
        throw std::runtime_error("Failed to read RadixTrie nodes");
    }

    trie.pool_.resize(static_cast<size_t>(pool_sz));
    if (pool_sz > 0) {
        if (!is.read(trie.pool_.data(), static_cast<std::streamsize>(pool_sz))) {
            throw std::runtime_error("Failed to read RadixTrie pool");
        }
    }

    // Validate memory safety: all node edge spans must reside inside string pool,
    // and all child/sibling references must index valid nodes.
    for (size_t i = 0; i < trie.nodes_.size(); ++i) {
        const auto& node = trie.nodes_[i];
        const uint64_t edge_off = node.edge_offset;
        const uint64_t edge_l = node.edge_len;

        if (edge_off > pool_sz || edge_l > pool_sz - edge_off) {
            throw std::runtime_error("Corrupted RadixTrie: node " + std::to_string(i) +
                                     " edge range [" + std::to_string(edge_off) + ", " +
                                     std::to_string(edge_off + edge_l) +
                                     "] exceeds pool size " + std::to_string(pool_sz));
        }

        if (node.first_child != 0 && node.first_child >= num_n) {
            throw std::runtime_error("Corrupted RadixTrie: invalid first_child link in node " +
                                     std::to_string(i));
        }
        if (node.next_sibling != 0 && node.next_sibling >= num_n) {
            throw std::runtime_error("Corrupted RadixTrie: invalid next_sibling link in node " +
                                     std::to_string(i));
        }
    }

    // Iterative cycle detection with bounded steps across both child and sibling chains
    std::vector<uint8_t> color(trie.nodes_.size(), 0);  // 0 = white, 1 = gray, 2 = black
    std::vector<uint32_t> stack;
    stack.reserve(64);
    stack.push_back(0);
    color[0] = 1;

    size_t total_steps = 0;
    const size_t max_total_steps = trie.nodes_.size() * 3 + 100;

    while (!stack.empty()) {
        if (++total_steps > max_total_steps) {
            throw std::runtime_error("Corrupted RadixTrie: cycle detected (step limit exceeded)");
        }

        uint32_t u = stack.back();
        uint32_t child = trie.nodes_[u].first_child;
        bool pushed = false;

        uint32_t sib = child;
        size_t sib_steps = 0;
        while (sib != 0) {
            if (++sib_steps > trie.nodes_.size()) {
                throw std::runtime_error("Corrupted RadixTrie: sibling cycle detected in node " +
                                         std::to_string(sib));
            }
            if (color[sib] == 1) {
                throw std::runtime_error("Corrupted RadixTrie: cycle detected involving node " +
                                         std::to_string(sib));
            }
            if (color[sib] == 0) {
                color[sib] = 1;
                stack.push_back(sib);
                pushed = true;
                break;
            }
            sib = trie.nodes_[sib].next_sibling;
        }

        if (!pushed) {
            color[u] = 2;
            stack.pop_back();
        }
    }

    return trie;
}

}  // namespace needlefish
