#include <gtest/gtest.h>
#include <random>
#include <vector>

#include "invidx/compression.hpp"
#include "invidx/postings.hpp"

using namespace needlefish;

TEST(CompressionTest, VarintRoundtrip) {
    std::vector<uint32_t> values = {0, 1, 127, 128, 255, 300, 16384, 1000000, 4294967295U};
    std::vector<uint8_t> encoded;
    Varint::encode_sequence(values, encoded);

    std::vector<uint32_t> decoded;
    Varint::decode_sequence(encoded, values.size(), decoded);

    EXPECT_EQ(values, decoded);
}

TEST(CompressionTest, BitPackingAllWidths) {
    std::mt19937 rng(42);

    for (uint8_t width = 0; width <= 32; ++width) {
        uint32_t in[128];
        const uint64_t max_val =
            (width == 32) ? 0xFFFFFFFFULL : (width == 0 ? 0 : ((1ULL << width) - 1ULL));

        for (size_t i = 0; i < 128; ++i) {
            if (width == 0) {
                in[i] = 0;
            } else if (width == 32) {
                in[i] = static_cast<uint32_t>(rng());
            } else {
                in[i] = static_cast<uint32_t>(rng() % (max_val + 1ULL));
            }
        }

        std::vector<uint8_t> packed(16 * width, 0);
        BitPacking::pack128(in, packed.data(), width);

        uint32_t out_scalar[128];
        BitPacking::unpack128_scalar(packed.data(), out_scalar, width);

        uint32_t out_simd[128];
        BitPacking::unpack128_simd(packed.data(), out_simd, width);

        for (size_t i = 0; i < 128; ++i) {
            EXPECT_EQ(in[i], out_scalar[i])
                << "Scalar mismatch at width " << int(width) << " index " << i;
            EXPECT_EQ(in[i], out_simd[i])
                << "SIMD mismatch at width " << int(width) << " index " << i;
        }
    }
}

TEST(PostingsTest, MultiBlockWriterReader) {
    PostingListWriter writer;
    const size_t num_docs = 500;

    std::vector<uint32_t> test_docs;
    std::vector<uint32_t> test_freqs;
    std::vector<std::vector<uint32_t>> test_positions;

    uint32_t curr_doc = 10;
    for (size_t i = 0; i < num_docs; ++i) {
        curr_doc += (i % 5 + 1);
        uint32_t freq = (i % 3 + 1);
        std::vector<uint32_t> positions;
        for (uint32_t p = 0; p < freq; ++p) {
            positions.push_back(p * 10 + 2);
        }

        test_docs.push_back(curr_doc);
        test_freqs.push_back(freq);
        test_positions.push_back(positions);

        writer.add_posting(curr_doc, freq, positions);
    }

    std::vector<uint8_t> postings_buf;
    std::vector<uint8_t> positions_buf;
    float max_scores[] = {4.5f, 4.5f, 4.5f, 4.5f};
    writer.finish(postings_buf, positions_buf, max_scores);

    PostingListReader reader(postings_buf, positions_buf, num_docs);

    // Sequential read
    for (size_t i = 0; i < num_docs; ++i) {
        ASSERT_TRUE(reader.valid());
        EXPECT_EQ(reader.doc_id(), test_docs[i]);
        EXPECT_EQ(reader.freq(), test_freqs[i]);

        std::vector<uint32_t> pos;
        reader.read_positions(pos);
        EXPECT_EQ(pos, test_positions[i]);

        reader.next();
    }
    EXPECT_FALSE(reader.valid());

    // Advance test
    PostingListReader skip_reader(postings_buf, positions_buf, num_docs);
    skip_reader.advance(test_docs[250]);
    EXPECT_TRUE(skip_reader.valid());
    EXPECT_EQ(skip_reader.doc_id(), test_docs[250]);

    skip_reader.advance(test_docs[499]);
    EXPECT_TRUE(skip_reader.valid());
    EXPECT_EQ(skip_reader.doc_id(), test_docs[499]);

    skip_reader.advance(test_docs.back() + 100);
    EXPECT_FALSE(skip_reader.valid());
}
