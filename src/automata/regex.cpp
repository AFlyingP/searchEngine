#include "automata/regex.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace needlefish {

RegexParser::RegexParser(std::string_view pattern) : pattern_(pattern), pos_(0) {}

char RegexParser::peek() const noexcept {
    if (pos_ < pattern_.size()) {
        return pattern_[pos_];
    }
    return '\0';
}

char RegexParser::advance() noexcept {
    if (pos_ < pattern_.size()) {
        return pattern_[pos_++];
    }
    return '\0';
}

bool RegexParser::match(char c) noexcept {
    if (peek() == c) {
        advance();
        return true;
    }
    return false;
}

bool RegexParser::at_end() const noexcept {
    return pos_ >= pattern_.size();
}

std::shared_ptr<RegexASTNode> RegexParser::parse(std::string_view pattern) {
    RegexParser parser(pattern);
    auto ast = parser.parse_expression();
    if (!parser.at_end()) {
        throw std::runtime_error("Unexpected trailing character in regex pattern: " +
                                 std::string(pattern));
    }
    return ast;
}

std::shared_ptr<RegexASTNode> RegexParser::parse_expression() {
    auto left = parse_concat();
    if (match('|')) {
        auto alt_node = std::make_shared<RegexASTNode>();
        alt_node->type = RegexASTType::Alt;
        alt_node->children.push_back(left);
        alt_node->children.push_back(parse_expression());
        return alt_node;
    }
    return left;
}

std::shared_ptr<RegexASTNode> RegexParser::parse_concat() {
    std::vector<std::shared_ptr<RegexASTNode>> children;
    while (!at_end() && peek() != '|' && peek() != ')') {
        auto atom = parse_repeat();
        if (atom->type != RegexASTType::Empty) {
            children.push_back(atom);
        }
    }

    if (children.empty()) {
        auto empty_node = std::make_shared<RegexASTNode>();
        empty_node->type = RegexASTType::Empty;
        return empty_node;
    }
    if (children.size() == 1) {
        return children[0];
    }

    auto concat_node = std::make_shared<RegexASTNode>();
    concat_node->type = RegexASTType::Concat;
    concat_node->children = std::move(children);
    return concat_node;
}

std::shared_ptr<RegexASTNode> RegexParser::parse_repeat() {
    auto atom = parse_atom();
    if (match('*')) {
        auto star = std::make_shared<RegexASTNode>();
        star->type = RegexASTType::Star;
        star->children.push_back(atom);
        return star;
    } else if (match('+')) {
        auto plus = std::make_shared<RegexASTNode>();
        plus->type = RegexASTType::Plus;
        plus->children.push_back(atom);
        return plus;
    } else if (match('?')) {
        auto q = std::make_shared<RegexASTNode>();
        q->type = RegexASTType::Question;
        q->children.push_back(atom);
        return q;
    } else if (match('{')) {
        // Parse {min,max} or {n}
        uint32_t min_c = 0;
        while (peek() >= '0' && peek() <= '9') {
            min_c = min_c * 10 + static_cast<uint32_t>(advance() - '0');
        }
        uint32_t max_c = min_c;
        if (match(',')) {
            if (peek() >= '0' && peek() <= '9') {
                max_c = 0;
                while (peek() >= '0' && peek() <= '9') {
                    max_c = max_c * 10 + static_cast<uint32_t>(advance() - '0');
                }
            } else {
                max_c = 1000;  // unbounded upper limit
            }
        }
        if (!match('}')) {
            throw std::runtime_error("Expected '}' in regex quantifier");
        }
        auto rep = std::make_shared<RegexASTNode>();
        rep->type = RegexASTType::Repeat;
        rep->min_count = min_c;
        rep->max_count = max_c;
        rep->children.push_back(atom);
        return rep;
    }
    return atom;
}

std::shared_ptr<RegexASTNode> RegexParser::parse_atom() {
    if (at_end()) {
        auto empty_node = std::make_shared<RegexASTNode>();
        empty_node->type = RegexASTType::Empty;
        return empty_node;
    }

    const char c = peek();
    if (c == '(') {
        advance();
        auto inner = parse_expression();
        if (!match(')')) {
            throw std::runtime_error("Unclosed parenthesis in regex pattern");
        }
        return inner;
    } else if (c == '.') {
        advance();
        auto wildcard = std::make_shared<RegexASTNode>();
        wildcard->type = RegexASTType::Wildcard;
        return wildcard;
    } else if (c == '[') {
        return parse_char_class();
    } else if (c == '\\') {
        advance();
        const char esc = advance();
        auto lit = std::make_shared<RegexASTNode>();
        lit->type = RegexASTType::Literal;
        if (esc == 'd') {
            lit->type = RegexASTType::CharClass;
            for (int d = '0'; d <= '9'; ++d)
                lit->char_set[static_cast<size_t>(d)] = true;
        } else if (esc == 'w') {
            lit->type = RegexASTType::CharClass;
            for (int a = 'a'; a <= 'z'; ++a)
                lit->char_set[static_cast<size_t>(a)] = true;
            for (int a = 'A'; a <= 'Z'; ++a)
                lit->char_set[static_cast<size_t>(a)] = true;
            for (int d = '0'; d <= '9'; ++d)
                lit->char_set[static_cast<size_t>(d)] = true;
            lit->char_set['_'] = true;
        } else if (esc == 's') {
            lit->type = RegexASTType::CharClass;
            lit->char_set[' '] = true;
            lit->char_set['\t'] = true;
            lit->char_set['\n'] = true;
            lit->char_set['\r'] = true;
        } else {
            lit->literal = static_cast<uint8_t>(esc);
        }
        return lit;
    } else if (c != '|' && c != ')' && c != '*' && c != '+' && c != '?' && c != '{') {
        advance();
        auto lit = std::make_shared<RegexASTNode>();
        lit->type = RegexASTType::Literal;
        lit->literal = static_cast<uint8_t>(c);
        return lit;
    }

    auto empty_node = std::make_shared<RegexASTNode>();
    empty_node->type = RegexASTType::Empty;
    return empty_node;
}

std::shared_ptr<RegexASTNode> RegexParser::parse_char_class() {
    advance();  // consume '['
    bool negated = false;
    if (match('^')) {
        negated = true;
    }

    auto node = std::make_shared<RegexASTNode>();
    node->type = RegexASTType::CharClass;

    while (!at_end() && peek() != ']') {
        char start_c = advance();
        if (start_c == '\\' && !at_end()) {
            start_c = advance();
        }
        if (peek() == '-' && pos_ + 1 < pattern_.size() && pattern_[pos_ + 1] != ']') {
            advance();  // consume '-'
            char end_c = advance();
            for (int c = static_cast<int>(static_cast<uint8_t>(start_c));
                 c <= static_cast<int>(static_cast<uint8_t>(end_c)); ++c) {
                node->char_set[static_cast<size_t>(c)] = true;
            }
        } else {
            node->char_set[static_cast<size_t>(static_cast<uint8_t>(start_c))] = true;
        }
    }

    if (!match(']')) {
        throw std::runtime_error("Unclosed character class in regex");
    }

    if (negated) {
        for (size_t i = 0; i < 256; ++i) {
            node->char_set[i] = !node->char_set[i];
        }
    }

    return node;
}

Regex::Regex(std::string_view pattern) : pattern_(pattern) {
    ast_ = RegexParser::parse(pattern_);
    compile();
}

int Regex::new_nfa_state() {
    int id = static_cast<int>(nfa_states_.size());
    nfa_states_.push_back(NFAState{.id = id, .is_match = false, .transitions = {}});
    return id;
}

void Regex::add_transition(int from, size_t symbol, int to) {
    nfa_states_[static_cast<size_t>(from)].transitions.emplace_back(symbol, to);
}

void Regex::compile() {
    nfa_states_.clear();
    auto frag = compile_node(ast_);
    nfa_start_ = frag.start_state;
    nfa_match_ = new_nfa_state();
    nfa_states_[static_cast<size_t>(nfa_match_)].is_match = true;

    for (int out : frag.out_states) {
        add_transition(out, NFAState::EPSILON, nfa_match_);
    }

    // Initialize DFA with initial epsilon closure
    std::vector<int> initial_nfa = {nfa_start_};
    std::vector<int> initial_closure;
    compute_epsilon_closure(initial_nfa, initial_closure);
    get_or_create_dfa_state(initial_closure);
}

Regex::NFAFragment Regex::compile_node(const std::shared_ptr<RegexASTNode>& node) {
    if (!node || node->type == RegexASTType::Empty) {
        int s = new_nfa_state();
        return NFAFragment{.start_state = s, .out_states = {s}};
    }

    if (node->type == RegexASTType::Literal) {
        int s0 = new_nfa_state();
        int s1 = new_nfa_state();
        add_transition(s0, node->literal, s1);
        return NFAFragment{.start_state = s0, .out_states = {s1}};
    }

    if (node->type == RegexASTType::Wildcard) {
        int s0 = new_nfa_state();
        int s1 = new_nfa_state();
        for (size_t c = 0; c < 256; ++c) {
            add_transition(s0, c, s1);
        }
        return NFAFragment{.start_state = s0, .out_states = {s1}};
    }

    if (node->type == RegexASTType::CharClass) {
        int s0 = new_nfa_state();
        int s1 = new_nfa_state();
        for (size_t c = 0; c < 256; ++c) {
            if (node->char_set[c]) {
                add_transition(s0, c, s1);
            }
        }
        return NFAFragment{.start_state = s0, .out_states = {s1}};
    }

    if (node->type == RegexASTType::Concat) {
        if (node->children.empty()) {
            int s = new_nfa_state();
            return NFAFragment{.start_state = s, .out_states = {s}};
        }
        auto curr_frag = compile_node(node->children[0]);
        for (size_t i = 1; i < node->children.size(); ++i) {
            auto next_frag = compile_node(node->children[i]);
            for (int out : curr_frag.out_states) {
                add_transition(out, NFAState::EPSILON, next_frag.start_state);
            }
            curr_frag.out_states = std::move(next_frag.out_states);
        }
        return curr_frag;
    }

    if (node->type == RegexASTType::Alt) {
        int s_start = new_nfa_state();
        std::vector<int> combined_out;
        for (const auto& child : node->children) {
            auto child_frag = compile_node(child);
            add_transition(s_start, NFAState::EPSILON, child_frag.start_state);
            combined_out.insert(combined_out.end(), child_frag.out_states.begin(),
                                child_frag.out_states.end());
        }
        return NFAFragment{.start_state = s_start, .out_states = std::move(combined_out)};
    }

    if (node->type == RegexASTType::Star) {
        int s_start = new_nfa_state();
        auto child_frag = compile_node(node->children[0]);
        add_transition(s_start, NFAState::EPSILON, child_frag.start_state);
        for (int out : child_frag.out_states) {
            add_transition(out, NFAState::EPSILON, child_frag.start_state);
        }
        std::vector<int> out = child_frag.out_states;
        out.push_back(s_start);
        return NFAFragment{.start_state = s_start, .out_states = std::move(out)};
    }

    if (node->type == RegexASTType::Plus) {
        int s_start = new_nfa_state();
        auto child_frag = compile_node(node->children[0]);
        add_transition(s_start, NFAState::EPSILON, child_frag.start_state);
        for (int out : child_frag.out_states) {
            add_transition(out, NFAState::EPSILON, child_frag.start_state);
        }
        return NFAFragment{.start_state = s_start, .out_states = child_frag.out_states};
    }

    if (node->type == RegexASTType::Question) {
        int s_start = new_nfa_state();
        auto child_frag = compile_node(node->children[0]);
        add_transition(s_start, NFAState::EPSILON, child_frag.start_state);
        std::vector<int> out = child_frag.out_states;
        out.push_back(s_start);
        return NFAFragment{.start_state = s_start, .out_states = std::move(out)};
    }

    if (node->type == RegexASTType::Repeat) {
        // Unroll repetition min_count .. max_count
        std::vector<std::shared_ptr<RegexASTNode>> unrolled;
        for (uint32_t i = 0; i < node->min_count; ++i) {
            unrolled.push_back(node->children[0]);
        }
        for (uint32_t i = node->min_count; i < node->max_count; ++i) {
            auto q = std::make_shared<RegexASTNode>();
            q->type = RegexASTType::Question;
            q->children.push_back(node->children[0]);
            unrolled.push_back(q);
        }
        auto concat = std::make_shared<RegexASTNode>();
        concat->type = RegexASTType::Concat;
        concat->children = std::move(unrolled);
        return compile_node(concat);
    }

    int s = new_nfa_state();
    return NFAFragment{.start_state = s, .out_states = {s}};
}

void Regex::compute_epsilon_closure(const std::vector<int>& states,
                                    std::vector<int>& closure) const {
    closure = states;
    std::vector<int> stack = states;

    std::vector<bool> visited(nfa_states_.size(), false);
    for (int s : states) {
        visited[static_cast<size_t>(s)] = true;
    }

    while (!stack.empty()) {
        int curr = stack.back();
        stack.pop_back();

        for (const auto& [sym, next] : nfa_states_[static_cast<size_t>(curr)].transitions) {
            if (sym == NFAState::EPSILON && !visited[static_cast<size_t>(next)]) {
                visited[static_cast<size_t>(next)] = true;
                closure.push_back(next);
                stack.push_back(next);
            }
        }
    }
    std::sort(closure.begin(), closure.end());
    closure.erase(std::unique(closure.begin(), closure.end()), closure.end());
}

int Regex::get_or_create_dfa_state(const std::vector<int>& nfa_set) {
    if (nfa_set.empty()) {
        return -1;
    }

    std::stringstream key_ss;
    for (int s : nfa_set) {
        key_ss << s << ",";
    }
    std::string key = key_ss.str();

    auto it = nfa_set_to_dfa_.find(key);
    if (it != nfa_set_to_dfa_.end()) {
        return it->second;
    }

    int dfa_id = static_cast<int>(dfa_states_.size());
    DFAState dfa_state{.id = dfa_id, .is_match = false, .transitions = {}};
    dfa_state.transitions.fill(-2);  // -2 indicates uncomputed transition

    for (int s : nfa_set) {
        if (nfa_states_[static_cast<size_t>(s)].is_match) {
            dfa_state.is_match = true;
            break;
        }
    }

    dfa_states_.push_back(dfa_state);
    nfa_set_to_dfa_[key] = dfa_id;

    // Lazily evaluate transitions on demand
    for (size_t c = 0; c < 256; ++c) {
        std::vector<int> target_nfa;
        for (int s : nfa_set) {
            for (const auto& [sym, next] : nfa_states_[static_cast<size_t>(s)].transitions) {
                if (sym == c) {
                    target_nfa.push_back(next);
                }
            }
        }
        if (!target_nfa.empty()) {
            std::vector<int> target_closure;
            compute_epsilon_closure(target_nfa, target_closure);
            dfa_states_[static_cast<size_t>(dfa_id)].transitions[c] =
                get_or_create_dfa_state(target_closure);
        } else {
            dfa_states_[static_cast<size_t>(dfa_id)].transitions[c] = -1;  // Dead state
        }
    }

    return dfa_id;
}

bool Regex::is_match(std::string_view text) const {
    int curr_dfa = 0;
    for (char ch : text) {
        const uint8_t byte = static_cast<uint8_t>(ch);
        if (curr_dfa < 0 || curr_dfa >= static_cast<int>(dfa_states_.size())) {
            return false;
        }
        curr_dfa = dfa_states_[static_cast<size_t>(curr_dfa)].transitions[byte];
        if (curr_dfa < 0) {
            return false;
        }
    }
    return curr_dfa >= 0 && dfa_states_[static_cast<size_t>(curr_dfa)].is_match;
}

std::pair<size_t, size_t> Regex::find_first(std::string_view text) const {
    for (size_t start = 0; start < text.size(); ++start) {
        int curr_dfa = 0;
        size_t last_match_end = std::string_view::npos;
        for (size_t end = start; end < text.size(); ++end) {
            const uint8_t byte = static_cast<uint8_t>(text[end]);
            curr_dfa = dfa_states_[static_cast<size_t>(curr_dfa)].transitions[byte];
            if (curr_dfa < 0) {
                break;
            }
            if (dfa_states_[static_cast<size_t>(curr_dfa)].is_match) {
                last_match_end = end + 1;
            }
        }
        if (last_match_end != std::string_view::npos) {
            return {start, last_match_end};
        }
    }
    return {std::string_view::npos, std::string_view::npos};
}

std::vector<std::pair<size_t, size_t>> Regex::find_all(std::string_view text) const {
    std::vector<std::pair<size_t, size_t>> matches;
    size_t start = 0;
    while (start < text.size()) {
        auto m = find_first(text.substr(start));
        if (m.first == std::string_view::npos) {
            break;
        }
        matches.emplace_back(start + m.first, start + m.second);
        start += (m.second > m.first ? m.second : 1);
    }
    return matches;
}

}  // namespace needlefish
