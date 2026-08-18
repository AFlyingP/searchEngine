#include <gtest/gtest.h>
#include "bitvector/bitvector.hpp"

#include <sstream>

using namespace needlefish;

TEST(BitVectorTest, BasicOperations) {
    BitVector bv(100);
    EXPECT_EQ(bv.size(), 100);
    EXPECT_FALSE(bv.empty());

    for (size_t i = 0; i < 100; ++i) {
        EXPECT_FALSE(bv[i]);
    }

    bv.set(0, true);
    bv.set(63, true);
    bv.set(64, true);
    bv.set(99, true);

    EXPECT_TRUE(bv[0]);
    EXPECT_TRUE(bv[63]);
    EXPECT_TRUE(bv[64]);
    EXPECT_TRUE(bv[99]);
    EXPECT_FALSE(bv[1]);
    EXPECT_FALSE(bv[62]);
    EXPECT_FALSE(bv[65]);

    bv.set(63, false);
    EXPECT_FALSE(bv[63]);
}

TEST(BitVectorTest, PushBackAndResize) {
    BitVector bv;
    EXPECT_TRUE(bv.empty());
    bv.push_back(true);
    bv.push_back(false);
    bv.push_back(true);
    EXPECT_EQ(bv.size(), 3);
    EXPECT_TRUE(bv[0]);
    EXPECT_FALSE(bv[1]);
    EXPECT_TRUE(bv[2]);

    bv.resize(130, true);
    EXPECT_EQ(bv.size(), 130);
    EXPECT_TRUE(bv[0]);
    EXPECT_FALSE(bv[1]);
    EXPECT_TRUE(bv[2]);
    EXPECT_TRUE(bv[3]);
    EXPECT_TRUE(bv[129]);
}

TEST(BitVectorTest, RankSelectEdgeCases) {
    // Empty bitvector
    BitVector empty_bv;
    RankSelectBitVector rs_empty(empty_bv);
    EXPECT_EQ(rs_empty.size(), 0);
    EXPECT_EQ(rs_empty.rank1(0), 0);
    EXPECT_EQ(rs_empty.rank0(0), 0);
    EXPECT_EQ(rs_empty.select1(1), 0);
    EXPECT_EQ(rs_empty.select0(1), 0);

    // Single 1-bit
    BitVector single_one(1, true);
    RankSelectBitVector rs_single_one(single_one);
    EXPECT_EQ(rs_single_one.rank1(0), 0);
    EXPECT_EQ(rs_single_one.rank1(1), 1);
    EXPECT_EQ(rs_single_one.rank0(1), 0);
    EXPECT_EQ(rs_single_one.select1(1), 0);
    EXPECT_EQ(rs_single_one.select0(1), 1);

    // Exact word & superblock boundaries (64, 512, 1024)
    BitVector bv(1024, false);
    bv.set(0, true);
    bv.set(63, true);
    bv.set(64, true);
    bv.set(511, true);
    bv.set(512, true);
    bv.set(1023, true);

    RankSelectBitVector rsbv(bv);
    EXPECT_EQ(rsbv.total_ones(), 6);
    EXPECT_EQ(rsbv.total_zeros(), 1018);

    EXPECT_EQ(rsbv.rank1(0), 0);
    EXPECT_EQ(rsbv.rank1(1), 1);
    EXPECT_EQ(rsbv.rank1(63), 1);
    EXPECT_EQ(rsbv.rank1(64), 2);
    EXPECT_EQ(rsbv.rank1(65), 3);
    EXPECT_EQ(rsbv.rank1(511), 3);
    EXPECT_EQ(rsbv.rank1(512), 4);
    EXPECT_EQ(rsbv.rank1(513), 5);
    EXPECT_EQ(rsbv.rank1(1023), 5);
    EXPECT_EQ(rsbv.rank1(1024), 6);

    EXPECT_EQ(rsbv.select1(1), 0);
    EXPECT_EQ(rsbv.select1(2), 63);
    EXPECT_EQ(rsbv.select1(3), 64);
    EXPECT_EQ(rsbv.select1(4), 511);
    EXPECT_EQ(rsbv.select1(5), 512);
    EXPECT_EQ(rsbv.select1(6), 1023);
}

TEST(BitVectorTest, SerializationRoundtrip) {
    BitVector bv(2000);
    for (size_t i = 0; i < 2000; i += 3) {
        bv.set(i, true);
    }
    RankSelectBitVector rsbv(bv);

    std::stringstream ss;
    rsbv.serialize(ss);

    RankSelectBitVector restored = RankSelectBitVector::deserialize(ss);
    EXPECT_EQ(restored.size(), rsbv.size());
    EXPECT_EQ(restored.total_ones(), rsbv.total_ones());

    for (size_t i = 0; i < 2000; ++i) {
        EXPECT_EQ(restored[i], rsbv[i]);
        EXPECT_EQ(restored.rank1(i), rsbv.rank1(i));
    }
}
