#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace needlefish {

/**
 * @brief AST node types for Regular Expressions.
 */
enum class RegexASTType {
    Empty,
    Literal,
    CharClass,
    Wildcard,  // '.'
    Concat,
    Alt,
    Star,
    Plus,
    Question,
    Repeat
};

struct RegexASTNode {
    RegexASTType type{RegexASTType::Empty};
    uint8_t literal{0};
    std::array<bool, 256> char_set{false};
    std::vector<std::shared_ptr<RegexASTNode>> children{};
    uint32_t min_count{0};
    uint32_t max_count{0};
};

/**
 * @brief Thompson NFA State.
 */
struct NFAState {
    int id{-1};
    bool is_match{false};
    // Character transitions: char -> set of target state ids
    // 256 is reserved for epsilon transition
    static constexpr size_t EPSILON = 256;
    std::vector<std::pair<size_t, int>> transitions{};
};

class RegexParser {
  public:
    static std::shared_ptr<RegexASTNode> parse(std::string_view pattern);

  private:
    explicit RegexParser(std::string_view pattern);

    std::shared_ptr<RegexASTNode> parse_expression();
    std::shared_ptr<RegexASTNode> parse_concat();
    std::shared_ptr<RegexASTNode> parse_repeat();
    std::shared_ptr<RegexASTNode> parse_atom();
    std::shared_ptr<RegexASTNode> parse_char_class();

    [[nodiscard]] bool match(char c) noexcept;
    [[nodiscard]] char peek() const noexcept;
    char advance() noexcept;
    [[nodiscard]] bool at_end() const noexcept;

    std::string_view pattern_;
    size_t pos_{0};
    size_t depth_{0};
};

/**
 * @brief High-performance Non-backtracking Byte-level Regex Engine.
 * Uses Thompson NFA -> Powerset DFA with linear-time O(n) matching.
 */
class Regex {
  public:
    explicit Regex(std::string_view pattern);

    [[nodiscard]] bool is_match(std::string_view text) const;
    [[nodiscard]] std::pair<size_t, size_t> find_first(std::string_view text) const;
    [[nodiscard]] std::vector<std::pair<size_t, size_t>> find_all(std::string_view text) const;

    [[nodiscard]] std::string_view pattern() const noexcept { return pattern_; }
    [[nodiscard]] const std::shared_ptr<RegexASTNode>& ast() const noexcept { return ast_; }

  private:
    struct NFAFragment {
        int start_state{0};
        std::vector<int> out_states{};
    };

    void compile();
    NFAFragment compile_node(const std::shared_ptr<RegexASTNode>& node);
    int new_nfa_state();
    void add_transition(int from, size_t symbol, int to);

    void compute_epsilon_closure(const std::vector<int>& states, std::vector<int>& closure) const;

    std::string pattern_;
    std::shared_ptr<RegexASTNode> ast_;

    std::vector<NFAState> nfa_states_;
    int nfa_start_{0};
    int nfa_match_{0};

    // DFA State Cache
    struct DFAState {
        int id{0};
        bool is_match{false};
        std::array<int, 256> transitions{};
    };

    std::vector<DFAState> dfa_states_{};
    std::unordered_map<std::string, int> nfa_set_to_dfa_{};
};

}  // namespace needlefish
