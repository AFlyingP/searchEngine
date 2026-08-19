#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace needlefish {

struct TermPayload {
    uint32_t term_id{std::numeric_limits<uint32_t>::max()};
    uint32_t doc_freq{0};
    uint64_t postings_offset{0};
    uint32_t postings_bytes{0};
    float max_term_score{0.0f};

    [[nodiscard]] bool valid() const noexcept {
        return term_id != std::numeric_limits<uint32_t>::max();
    }
};

/**
 * @brief Contiguous Flattened Radix Trie Node (32 bytes aligned).
 */
struct alignas(8) RadixNode {
    uint32_t edge_offset{0};   // Offset into string pool
    uint16_t edge_len{0};      // Length of edge string
    uint16_t flags{0};         // Bit 0 = is_terminal
    uint32_t first_child{0};   // 0 = None
    uint32_t next_sibling{0};  // 0 = None
    TermPayload payload{};
};

/**
 * @brief Compressed Radix Trie with contiguous flat array representation.
 */
class RadixTrie {
  public:
    RadixTrie();

    void insert(std::string_view key, const TermPayload& payload);

    [[nodiscard]] TermPayload lookup(std::string_view key) const noexcept;

    /**
     * @brief Find all terms starting with a prefix.
     * @param max_results Maximum number of results to return.
     */
    [[nodiscard]] std::vector<std::pair<std::string, TermPayload>> prefix_search(
        std::string_view prefix, size_t max_results = 50) const;

    /**
     * @brief Lockstep traversal with a generic DFA / Automaton.
     * Visitor signature: bool(std::string_view term, const TermPayload& payload, int state)
     */
    void traverse_with_dfa(const std::function<int(int state, char c)>& step_fn,
                           const std::function<bool(int state)>& is_accept_fn,
                           const std::function<bool(int state)>& can_match_fn,
                           const std::function<bool(std::string_view term,
                                                    const TermPayload& payload)>& visitor) const;

    [[nodiscard]] size_t num_nodes() const noexcept { return nodes_.size(); }
    [[nodiscard]] size_t num_terms() const noexcept { return num_terms_; }
    [[nodiscard]] std::span<const RadixNode> nodes() const noexcept { return nodes_; }
    [[nodiscard]] std::string_view string_pool() const noexcept { return pool_; }

    void serialize(std::ostream& os) const;
    static RadixTrie deserialize(std::istream& is);

  private:
    void collect_subtree(uint32_t node_idx, std::string& current_prefix,
                         std::vector<std::pair<std::string, TermPayload>>& out,
                         size_t max_results) const;

    void dfa_dfs(uint32_t node_idx, int state, std::string& current_prefix,
                 const std::function<int(int state, char c)>& step_fn,
                 const std::function<bool(int state)>& is_accept_fn,
                 const std::function<bool(int state)>& can_match_fn,
                 const std::function<bool(std::string_view term, const TermPayload& payload)>&
                     visitor) const;

    std::vector<RadixNode> nodes_{};
    std::string pool_{};
    size_t num_terms_{0};
};

}  // namespace needlefish
