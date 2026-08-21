#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include <vector>

#include "util/analyzer.hpp"
#include "util/porter_stemmer.hpp"
#include "util/utf8.hpp"

using namespace needlefish;

TEST(Utf8Test, BasicAscii) {
    auto cps = Utf8Decoder::decode("Hello, World!");
    EXPECT_EQ(cps.size(), 13);
    EXPECT_EQ(Utf8Decoder::encode(cps), "Hello, World!");
}

TEST(Utf8Test, MultiByteAndReplacement) {
    // Valid 2-byte, 3-byte, 4-byte
    std::string text = "café 🚀 日本語";
    auto cps = Utf8Decoder::decode(text);
    EXPECT_EQ(Utf8Decoder::encode(cps), text);

    // Invalid truncated sequence should produce U+FFFD replacement without throwing
    std::string invalid = "\xC0\xAF\xFF";
    auto invalid_cps = Utf8Decoder::decode(invalid);
    EXPECT_FALSE(invalid_cps.empty());
}

TEST(Utf8Test, CaseFolding) {
    EXPECT_EQ(Utf8Decoder::to_lower_utf8("HELLO WORLD"), "hello world");
    EXPECT_EQ(Utf8Decoder::to_lower_utf8("ÉlÉpHaNt"), "éléphant");
}

TEST(PorterStemmerTest, BasicWords) {
    EXPECT_EQ(PorterStemmer::stem("caresses"), "caress");
    EXPECT_EQ(PorterStemmer::stem("ponies"), "poni");
    EXPECT_EQ(PorterStemmer::stem("ties"), "ti");
    EXPECT_EQ(PorterStemmer::stem("caress"), "caress");
    EXPECT_EQ(PorterStemmer::stem("cats"), "cat");
    EXPECT_EQ(PorterStemmer::stem("feed"), "feed");
    EXPECT_EQ(PorterStemmer::stem("agreed"), "agre");
    EXPECT_EQ(PorterStemmer::stem("plastered"), "plaster");
    EXPECT_EQ(PorterStemmer::stem("motoring"), "motor");
    EXPECT_EQ(PorterStemmer::stem("sing"), "sing");
}

TEST(PorterStemmerTest, OfficialGoldenSuite) {
    std::vector<std::filesystem::path> search_prefixes = {
        ".", "..", "../..", "../../..", "../../../.."
    };
    std::filesystem::path voc_path, out_path;
    for (const auto& prefix : search_prefixes) {
        auto v = prefix / std::filesystem::path("tests/golden/porter/voc.txt");
        auto o = prefix / std::filesystem::path("tests/golden/porter/output.txt");
        if (std::filesystem::exists(v) && std::filesystem::exists(o)) {
            voc_path = v;
            out_path = o;
            break;
        }
    }

    std::ifstream voc_file(voc_path);
    std::ifstream out_file(out_path);

    if (!voc_file.is_open() || !out_file.is_open()) {
        GTEST_SKIP() << "Porter test vectors not found at " << voc_path;
    }

    std::string word;
    std::string expected;
    size_t count = 0;
    size_t mismatches = 0;

    while (std::getline(voc_file, word) && std::getline(out_file, expected)) {
        if (word.empty())
            continue;
        std::string actual;
        try {
            actual = PorterStemmer::stem(word);
        } catch (...) {
            std::cerr << "Crashed on word: " << word << "\n";
            FAIL() << "Crashed on word: " << word;
        }
        if (actual != expected) {
            mismatches++;
            if (mismatches <= 5) {
                std::cerr << "Mismatch for '" << word << "': expected '" << expected << "', got '"
                          << actual << "'\n";
            }
        }
        count++;
    }

    EXPECT_GT(count, 20000);
    EXPECT_EQ(mismatches, 0) << "Stemming mismatches in official Porter test set!";
}

TEST(AnalyzerTest, TokenizationAndStopwords) {
    Analyzer analyzer(true, true);
    std::string sample = "The quick brown fox jumps over the lazy dog.";
    auto tokens = analyzer.analyze(sample);

    // "The" (pos 0), "over" (pos 5), "the" (pos 6) are stopwords
    std::vector<std::string> expected_terms = {"quick", "brown", "fox", "jump", "lazi", "dog"};
    std::vector<uint32_t> expected_positions = {1, 2, 3, 4, 7, 8};
    ASSERT_EQ(tokens.size(), expected_terms.size());

    for (size_t i = 0; i < tokens.size(); ++i) {
        EXPECT_EQ(tokens[i].term, expected_terms[i]);
        EXPECT_EQ(tokens[i].position, expected_positions[i]);
    }
}
