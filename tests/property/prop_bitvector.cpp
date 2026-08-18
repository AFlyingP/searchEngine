#include <gtest/gtest.h>
#include <random>
#include <vector>

#include "bitvector/bitvector.hpp"

using namespace needlefish;

class BitVectorOracle {
  public:
    explicit BitVectorOracle(const std::vector<bool>& bits) : bits_(bits) {
        prefix_ones_.resize(bits.size() + 1, 0);
        for (size_t i = 0; i < bits.size(); ++i) {
            prefix_ones_[i + 1] = prefix_ones_[i] + (bits[i] ? 1 : 0);
            if (bits[i]) {
                one_positions_.push_back(i);
            } else {
                zero_positions_.push_back(i);
            }
        }
    }

    size_t rank1(size_t index) const {
        if (index >= bits_.size())
            return prefix_ones_.back();
        return prefix_ones_[index];
    }

    size_t rank0(size_t index) const {
        if (index >= bits_.size())
            return bits_.size() - prefix_ones_.back();
        return index - rank1(index);
    }

    size_t select1(size_t k) const {
        if (k == 0 || k > one_positions_.size())
            return bits_.size();
        return one_positions_[k - 1];
    }

    size_t select0(size_t k) const {
        if (k == 0 || k > zero_positions_.size())
            return bits_.size();
        return zero_positions_[k - 1];
    }

  private:
    std::vector<bool> bits_;
    std::vector<size_t> prefix_ones_;
    std::vector<size_t> one_positions_;
    std::vector<size_t> zero_positions_;
};

void run_density_property_test(double density, size_t num_bits, size_t num_ops, uint32_t seed) {
    std::mt19937_64 rng(seed);
    std::bernoulli_distribution bit_dist(density);

    std::vector<bool> raw_bits(num_bits);
    BitVector bv(num_bits);
    for (size_t i = 0; i < num_bits; ++i) {
        bool b = bit_dist(rng);
        raw_bits[i] = b;
        bv.set(i, b);
    }

    RankSelectBitVector rsbv(std::move(bv));
    BitVectorOracle oracle(raw_bits);

    std::uniform_int_distribution<size_t> idx_dist(0, num_bits);
    std::uniform_int_distribution<int> op_dist(0, 3);

    size_t total_ones = rsbv.total_ones();
    size_t total_zeros = rsbv.total_zeros();

    std::uniform_int_distribution<size_t> k1_dist(1, std::max<size_t>(1, total_ones));
    std::uniform_int_distribution<size_t> k0_dist(1, std::max<size_t>(1, total_zeros));

    for (size_t op = 0; op < num_ops; ++op) {
        int op_type = op_dist(rng);
        if (op_type == 0) {
            size_t idx = idx_dist(rng);
            ASSERT_EQ(rsbv.rank1(idx), oracle.rank1(idx)) << "Mismatch rank1 at index " << idx;
        } else if (op_type == 1) {
            size_t idx = idx_dist(rng);
            ASSERT_EQ(rsbv.rank0(idx), oracle.rank0(idx)) << "Mismatch rank0 at index " << idx;
        } else if (op_type == 2) {
            if (total_ones > 0) {
                size_t k = k1_dist(rng);
                ASSERT_EQ(rsbv.select1(k), oracle.select1(k)) << "Mismatch select1 for k=" << k;
            }
        } else {
            if (total_zeros > 0) {
                size_t k = k0_dist(rng);
                ASSERT_EQ(rsbv.select0(k), oracle.select0(k)) << "Mismatch select0 for k=" << k;
            }
        }
    }
}

TEST(BitVectorPropertyTest, MillionOpsAcrossDensities) {
    // 1% density (sparse) - 333,334 ops
    run_density_property_test(0.01, 100000, 333334, 42);
    // 50% density (uniform) - 333,333 ops
    run_density_property_test(0.50, 100000, 333333, 43);
    // 99% density (dense) - 333,333 ops
    run_density_property_test(0.99, 100000, 333333, 44);
}
