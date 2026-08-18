#include "bitvector/bitvector.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace needlefish {

BitVector::BitVector(size_t num_bits, bool default_val)
    : num_bits_(num_bits), words_((num_bits + 63) / 64, default_val ? ~0ULL : 0ULL) {
    if (default_val && num_bits_ % 64 != 0) {
        const size_t rem = num_bits_ % 64;
        words_.back() = (1ULL << rem) - 1ULL;
    }
}

void BitVector::resize(size_t num_bits, bool val) {
    const size_t old_words = words_.size();
    const size_t new_words = (num_bits + 63) / 64;
    words_.resize(new_words, val ? ~0ULL : 0ULL);
    if (val && num_bits > num_bits_) {
        // Fix up bits in transition word
        const size_t start_word = num_bits_ / 64;
        const size_t start_rem = num_bits_ % 64;
        if (start_word < old_words && start_rem != 0) {
            words_[start_word] |= (~0ULL << start_rem);
        }
    }
    num_bits_ = num_bits;
    if (new_words > 0 && num_bits_ % 64 != 0) {
        const size_t rem = num_bits_ % 64;
        words_.back() &= (1ULL << rem) - 1ULL;
    }
}

void BitVector::push_back(bool bit) {
    const size_t index = num_bits_++;
    const size_t word_idx = index / 64;
    if (word_idx >= words_.size()) {
        words_.push_back(0);
    }
    if (bit) {
        words_[word_idx] |= (1ULL << (index % 64));
    }
}

void BitVector::serialize(std::ostream& os) const {
    const uint64_t n = static_cast<uint64_t>(num_bits_);
    os.write(reinterpret_cast<const char*>(&n), sizeof(n));
    const uint64_t nw = static_cast<uint64_t>(words_.size());
    os.write(reinterpret_cast<const char*>(&nw), sizeof(nw));
    if (nw > 0) {
        os.write(reinterpret_cast<const char*>(words_.data()),
                 static_cast<std::streamsize>(nw * sizeof(uint64_t)));
    }
}

BitVector BitVector::deserialize(std::istream& is) {
    BitVector bv;
    uint64_t n = 0;
    uint64_t nw = 0;
    if (!is.read(reinterpret_cast<char*>(&n), sizeof(n))) {
        throw std::runtime_error("Failed to read BitVector size");
    }
    if (!is.read(reinterpret_cast<char*>(&nw), sizeof(nw))) {
        throw std::runtime_error("Failed to read BitVector word count");
    }
    bv.num_bits_ = static_cast<size_t>(n);
    bv.words_.resize(static_cast<size_t>(nw));
    if (nw > 0) {
        if (!is.read(reinterpret_cast<char*>(bv.words_.data()),
                     static_cast<std::streamsize>(nw * sizeof(uint64_t)))) {
            throw std::runtime_error("Failed to read BitVector words");
        }
    }
    return bv;
}

RankSelectBitVector::RankSelectBitVector(BitVector bv) : bv_(std::move(bv)), size_(bv_.size()) {
    build_directory();
}

void RankSelectBitVector::build_directory() {
    const size_t total_superblocks = (size_ + 511) / 512 + 1;
    directory_.resize(total_superblocks);

    uint64_t running_rank = 0;
    const auto& words = bv_.raw_words();
    const size_t num_words = words.size();

    for (size_t s = 0; s < total_superblocks; ++s) {
        directory_[s].superblock_rank = running_rank;
        uint16_t sub_rel = 0;
        for (size_t w = 0; w < 8; ++w) {
            directory_[s].subblock_ranks[w] = sub_rel;
            const size_t word_idx = s * 8 + w;
            if (word_idx < num_words) {
                const uint16_t pc = static_cast<uint16_t>(std::popcount(words[word_idx]));
                sub_rel = static_cast<uint16_t>(sub_rel + pc);
            }
        }
        running_rank += sub_rel;
    }
    total_ones_ = running_rank;
}

size_t RankSelectBitVector::rank1(size_t index) const noexcept {
    if (index >= size_) {
        return total_ones_;
    }
    if (index == 0) {
        return 0;
    }

    const size_t s = index / 512;
    const size_t rem_in_sb = index % 512;
    const size_t w = rem_in_sb / 64;
    const size_t b = rem_in_sb % 64;

    size_t result = directory_[s].superblock_rank + directory_[s].subblock_ranks[w];
    if (b > 0) {
        const size_t word_idx = s * 8 + w;
        const uint64_t mask = (1ULL << b) - 1ULL;
        result += static_cast<size_t>(std::popcount(bv_.raw_words()[word_idx] & mask));
    }
    return result;
}

size_t RankSelectBitVector::select1(size_t k) const noexcept {
    if (k == 0 || k > total_ones_) {
        return size_;
    }

    // Binary search over superblocks
    size_t low_s = 0;
    size_t high_s = directory_.size() - 1;
    while (low_s < high_s) {
        const size_t mid = low_s + (high_s - low_s + 1) / 2;
        if (directory_[mid].superblock_rank < k) {
            low_s = mid;
        } else {
            high_s = mid - 1;
        }
    }

    const size_t s = low_s;
    const uint64_t sb_rank = directory_[s].superblock_rank;
    const size_t rem_k = k - sb_rank;

    // Search subblock
    size_t w = 0;
    for (size_t i = 1; i < 8; ++i) {
        if (directory_[s].subblock_ranks[i] < rem_k) {
            w = i;
        } else {
            break;
        }
    }

    const size_t word_idx = s * 8 + w;
    const uint64_t in_word_k = rem_k - directory_[s].subblock_ranks[w];
    const uint64_t word = bv_.raw_words()[word_idx];
    const size_t bit_in_word = select_in_word(word, in_word_k);

    return word_idx * 64 + bit_in_word;
}

size_t RankSelectBitVector::select0(size_t k) const noexcept {
    const size_t total_zeros = size_ - total_ones_;
    if (k == 0 || k > total_zeros) {
        return size_;
    }

    // Binary search over superblocks for 0-rank
    size_t low_s = 0;
    size_t high_s = directory_.size() - 1;
    while (low_s < high_s) {
        const size_t mid = low_s + (high_s - low_s + 1) / 2;
        const size_t zeros_before = mid * 512 - directory_[mid].superblock_rank;
        if (zeros_before < k) {
            low_s = mid;
        } else {
            high_s = mid - 1;
        }
    }

    const size_t s = low_s;
    const size_t sb_zeros = s * 512 - directory_[s].superblock_rank;
    const size_t rem_k = k - sb_zeros;

    size_t w = 0;
    for (size_t i = 1; i < 8; ++i) {
        const size_t sub_zeros = i * 64 - directory_[s].subblock_ranks[i];
        if (sub_zeros < rem_k) {
            w = i;
        } else {
            break;
        }
    }

    const size_t word_idx = s * 8 + w;
    const size_t sub_zeros = w * 64 - directory_[s].subblock_ranks[w];
    const uint64_t in_word_k = rem_k - sub_zeros;
    const uint64_t word = ~bv_.raw_words()[word_idx];
    const size_t bit_in_word = select_in_word(word, in_word_k);

    return word_idx * 64 + bit_in_word;
}

void RankSelectBitVector::serialize(std::ostream& os) const {
    bv_.serialize(os);
    const uint64_t sz = static_cast<uint64_t>(size_);
    const uint64_t to = static_cast<uint64_t>(total_ones_);
    os.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    os.write(reinterpret_cast<const char*>(&to), sizeof(to));
    const uint64_t dir_sz = static_cast<uint64_t>(directory_.size());
    os.write(reinterpret_cast<const char*>(&dir_sz), sizeof(dir_sz));
    if (dir_sz > 0) {
        os.write(reinterpret_cast<const char*>(directory_.data()),
                 static_cast<std::streamsize>(dir_sz * sizeof(DirectoryEntry)));
    }
}

RankSelectBitVector RankSelectBitVector::deserialize(std::istream& is) {
    RankSelectBitVector rsbv;
    rsbv.bv_ = BitVector::deserialize(is);
    uint64_t sz = 0;
    uint64_t to = 0;
    uint64_t dir_sz = 0;
    if (!is.read(reinterpret_cast<char*>(&sz), sizeof(sz))) {
        throw std::runtime_error("Failed to read RankSelectBitVector size");
    }
    if (!is.read(reinterpret_cast<char*>(&to), sizeof(to))) {
        throw std::runtime_error("Failed to read RankSelectBitVector total_ones");
    }
    if (!is.read(reinterpret_cast<char*>(&dir_sz), sizeof(dir_sz))) {
        throw std::runtime_error("Failed to read RankSelectBitVector directory size");
    }
    rsbv.size_ = static_cast<size_t>(sz);
    rsbv.total_ones_ = static_cast<size_t>(to);
    rsbv.directory_.resize(static_cast<size_t>(dir_sz));
    if (dir_sz > 0) {
        if (!is.read(reinterpret_cast<char*>(rsbv.directory_.data()),
                     static_cast<std::streamsize>(dir_sz * sizeof(DirectoryEntry)))) {
            throw std::runtime_error("Failed to read RankSelectBitVector directory data");
        }
    }
    return rsbv;
}

}  // namespace needlefish
