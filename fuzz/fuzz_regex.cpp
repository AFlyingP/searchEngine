#include "automata/regex.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 2048) {
        return 0;
    }

    std::string_view input(reinterpret_cast<const char*>(data), size);

    try {
        needlefish::Regex regex(input);
        (void)regex.is_match("hello world");
        (void)regex.is_match("needlefish");
        (void)regex.is_match("");
    } catch (...) {
        // Expected on invalid regex syntax or limits
    }

    return 0;
}

#ifndef NEEDLEFISH_LIBFUZZER
int main() {
    std::vector<std::string> test_inputs = {
        "",
        "a+b*c?",
        "(abc|def)+",
        "[a-z0-9_]+",
        "a{2,5}",
        "a{100}",
        "a{200}",
        std::string(600, 'a'),
        "(a*)*",
        "((((a))))",
        "[^a-z]",
        "\\d+\\w*\\s?"
    };

    for (const auto& input : test_inputs) {
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    }

    return 0;
}
#endif
