#include "wavelet/wavelet_tree.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace needlefish {

WaveletTree::WaveletTree(std::span<const uint8_t> text) {
    build(text);
}

WaveletTree::WaveletTree(std::string_view text) {
    build(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

size_t WaveletTree::prefix_count(size_t level, size_t node_prefix) const noexcept {
    if (level > 8 || node_prefix >= node_offsets_[level].size()) {
        return size_;
    }
    return node_offsets_[level][node_prefix];
}

void WaveletTree::build(std::span<const uint8_t> text) {
    size_ = text.size();
    counts_.fill(0);
    c_table_.fill(0);

    for (uint8_t c : text) {
        counts_[c]++;
    }

    size_t running_c = 0;
    for (size_t i = 0; i < 256; ++i) {
        c_table_[i] = running_c;
        running_c += counts_[i];
    }

    // Build node_offsets_ table for all 9 levels (0..8)
    // Level l has 2^l nodes + 1 sentinel entry at the end = 2^l + 1 entries
    node_offsets_.resize(9);
    for (size_t l = 0; l <= 8; ++l) {
        const size_t num_nodes = 1ULL << l;
        node_offsets_[l].resize(num_nodes + 1, 0);

        const size_t shift = 8 - l;
        size_t cum = 0;
        for (size_t p = 0; p < num_nodes; ++p) {
            node_offsets_[l][p] = cum;
            // Sum all counts where (c >> shift) == p
            if (l == 8) {
                cum += counts_[p];
            } else {
                const size_t start_c = p << shift;
                const size_t end_c = (p + 1) << shift;
                for (size_t c = start_c; c < end_c && c < 256; ++c) {
                    cum += counts_[c];
                }
            }
        }
        node_offsets_[l][num_nodes] = cum;
    }

    if (size_ == 0) {
        return;
    }

    std::vector<uint8_t> curr(text.begin(), text.end());
    std::vector<uint8_t> next(size_);

    for (size_t l = 0; l < 8; ++l) {
        const size_t shift = 7 - l;
        const size_t num_nodes = 1ULL << l;
        BitVector bv(size_);

        // Write pointers for children in level l + 1
        std::vector<size_t> child_ptrs(1ULL << (l + 1));
        for (size_t p = 0; p < (1ULL << (l + 1)); ++p) {
            child_ptrs[p] = node_offsets_[l + 1][p];
        }

        for (size_t p = 0; p < num_nodes; ++p) {
            const size_t node_start = node_offsets_[l][p];
            const size_t node_end = node_offsets_[l][p + 1];

            const size_t left_child = p * 2;
            const size_t right_child = p * 2 + 1;

            for (size_t i = node_start; i < node_end; ++i) {
                const uint8_t byte_val = curr[i];
                const bool bit = ((byte_val >> shift) & 1) != 0;
                bv.set(i, bit);
                if (!bit) {
                    next[child_ptrs[left_child]++] = byte_val;
                } else {
                    next[child_ptrs[right_child]++] = byte_val;
                }
            }
        }

        levels_[l] = RankSelectBitVector(std::move(bv));
        curr = next;
    }
}

uint8_t WaveletTree::access(size_t index) const noexcept {
    if (index >= size_) {
        return 0;
    }

    uint8_t result = 0;
    size_t pos = index;
    size_t p = 0;

    for (size_t l = 0; l < 8; ++l) {
        const size_t start = node_offsets_[l][p];
        const bool bit = levels_[l].get(pos);
        result = static_cast<uint8_t>((result << 1) | (bit ? 1 : 0));

        if (!bit) {
            const size_t offset = levels_[l].rank0(pos) - levels_[l].rank0(start);
            p = p * 2;
            pos = node_offsets_[l + 1][p] + offset;
        } else {
            const size_t offset = levels_[l].rank1(pos) - levels_[l].rank1(start);
            p = p * 2 + 1;
            pos = node_offsets_[l + 1][p] + offset;
        }
    }

    return result;
}

size_t WaveletTree::rank(uint8_t c, size_t index) const noexcept {
    if (index == 0 || size_ == 0) {
        return 0;
    }
    if (index > size_) {
        index = size_;
    }

    size_t pos = index;
    size_t p = 0;

    for (size_t l = 0; l < 8; ++l) {
        const size_t start = node_offsets_[l][p];
        const bool bit = ((c >> (7 - l)) & 1) != 0;

        if (!bit) {
            const size_t offset = levels_[l].rank0(pos) - levels_[l].rank0(start);
            p = p * 2;
            pos = node_offsets_[l + 1][p] + offset;
        } else {
            const size_t offset = levels_[l].rank1(pos) - levels_[l].rank1(start);
            p = p * 2 + 1;
            pos = node_offsets_[l + 1][p] + offset;
        }
    }

    return pos - c_table_[c];
}

size_t WaveletTree::select(uint8_t c, size_t k) const noexcept {
    if (k == 0 || k > counts_[c] || size_ == 0) {
        return size_;
    }

    size_t p = c;
    size_t offset = k - 1;

    for (size_t l = 8; l > 0; --l) {
        const size_t level_idx = l - 1;
        const size_t parent_p = p / 2;
        const size_t start = node_offsets_[level_idx][parent_p];
        const bool bit = (p & 1) != 0;

        size_t pos = 0;
        if (!bit) {
            const size_t target_k = levels_[level_idx].rank0(start) + offset + 1;
            pos = levels_[level_idx].select0(target_k);
        } else {
            const size_t target_k = levels_[level_idx].rank1(start) + offset + 1;
            pos = levels_[level_idx].select1(target_k);
        }

        offset = pos - start;
        p = parent_p;
    }

    return offset;
}

void WaveletTree::serialize(std::ostream& os) const {
    const uint64_t sz = static_cast<uint64_t>(size_);
    os.write(reinterpret_cast<const char*>(&sz), sizeof(sz));

    for (size_t i = 0; i < 256; ++i) {
        const uint64_t cnt = static_cast<uint64_t>(counts_[i]);
        os.write(reinterpret_cast<const char*>(&cnt), sizeof(cnt));
    }
    for (size_t i = 0; i < 256; ++i) {
        const uint64_t c = static_cast<uint64_t>(c_table_[i]);
        os.write(reinterpret_cast<const char*>(&c), sizeof(c));
    }

    for (size_t l = 0; l < 8; ++l) {
        levels_[l].serialize(os);
    }
}

WaveletTree WaveletTree::deserialize(std::istream& is) {
    WaveletTree wt;
    uint64_t sz = 0;
    if (!is.read(reinterpret_cast<char*>(&sz), sizeof(sz))) {
        throw std::runtime_error("Failed to read WaveletTree size");
    }
    wt.size_ = static_cast<size_t>(sz);

    for (size_t i = 0; i < 256; ++i) {
        uint64_t cnt = 0;
        if (!is.read(reinterpret_cast<char*>(&cnt), sizeof(cnt))) {
            throw std::runtime_error("Failed to read WaveletTree counts");
        }
        wt.counts_[i] = static_cast<size_t>(cnt);
    }
    for (size_t i = 0; i < 256; ++i) {
        uint64_t c = 0;
        if (!is.read(reinterpret_cast<char*>(&c), sizeof(c))) {
            throw std::runtime_error("Failed to read WaveletTree c_table");
        }
        wt.c_table_[i] = static_cast<size_t>(c);
    }

    // Rebuild node_offsets_
    wt.node_offsets_.resize(9);
    for (size_t l = 0; l <= 8; ++l) {
        const size_t num_nodes = 1ULL << l;
        wt.node_offsets_[l].resize(num_nodes + 1, 0);

        const size_t shift = 8 - l;
        size_t cum = 0;
        for (size_t p = 0; p < num_nodes; ++p) {
            wt.node_offsets_[l][p] = cum;
            if (l == 8) {
                cum += wt.counts_[p];
            } else {
                const size_t start_c = p << shift;
                const size_t end_c = (p + 1) << shift;
                for (size_t c = start_c; c < end_c && c < 256; ++c) {
                    cum += wt.counts_[c];
                }
            }
        }
        wt.node_offsets_[l][num_nodes] = cum;
    }

    for (size_t l = 0; l < 8; ++l) {
        wt.levels_[l] = RankSelectBitVector::deserialize(is);
    }

    return wt;
}

}  // namespace needlefish
