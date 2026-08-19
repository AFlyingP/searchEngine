#include "util/analyzer.hpp"
#include "util/porter_stemmer.hpp"
#include "util/utf8.hpp"
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 8192) {
        return 0;
    }

    std::string_view input(reinterpret_cast<const char*>(data), size);

    // 1. Fuzz UTF-8 decoder & case folding
    auto codepoints = needlefish::Utf8Decoder::decode(input);
    (void)needlefish::Utf8Decoder::encode(codepoints);
    (void)needlefish::Utf8Decoder::to_lower_utf8(input);

    // 2. Fuzz Porter stemmer
    (void)needlefish::PorterStemmer::stem(input);

    // 3. Fuzz document analyzer
    needlefish::Analyzer analyzer;
    (void)analyzer.analyze(input);
    (void)analyzer.normalize_term(input);

    return 0;
}

#ifndef NEEDLEFISH_LIBFUZZER
int main() {
    std::vector<std::string> test_inputs = {
        "",
        "The quick brown fox jumps over the lazy dog.",
        "\xEF\xBB\xBF\xC3\x28\xED\xA0\x80\xF0\x90\x80\x80",
        std::string(4000, '\xFF'),
        "programming programmers programmable programmingly program",
        "123.456.789!@#$%^&*()_+-=[]{}|;':\",./<>?",
        "\0\0\0embedded\0nulls\0\0\0"
    };

    for (const auto& input : test_inputs) {
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    }

    return 0;
}
#endif
