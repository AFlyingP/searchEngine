#include "fm/fm_index.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace needlefish {

FMIndex::FMIndex(std::span<const uint8_t> text, size_t sample_rate) {
    build(text, sample_rate);
}

FMIndex::FMIndex(std::string_view text, size_t sample_rate) {
    build(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
          sample_rate);
}

void FMIndex::build(std::span<const uint8_t> text, size_t sample_rate) {
    text_size_ = text.size();
    sample_rate_ = (sample_rate == 0) ? DEFAULT_SAMPLE_RATE : sample_rate;
    bwt_size_ = text_size_ + 1;
    c_table_.fill(0);

    if (text_size_ == 0) {
        return;
    }

    is_64bit_ = bwt_size_ >= static_cast<size_t>(std::numeric_limits<int32_t>::max());

    // 1. Build Suffix Array with sentinel 0
    std::vector<uint8_t> bwt(bwt_size_);
    BitVector sampled_bv(bwt_size_);

    const size_t num_inv_samples = (text_size_ + sample_rate_) / sample_rate_ + 1;
    inv_sampled_rows_.assign(num_inv_samples, 0);

    if (!is_64bit_) {
        std::vector<int32_t> s(bwt_size_);
        for (size_t i = 0; i < text_size_; ++i) {
            s[i] = static_cast<int32_t>(text[i]);
        }
        s[text_size_] = 0;

        auto sa = SaisBuilder<int32_t>::build(s, 258);

        sampled_sa_32_.clear();
        for (size_t i = 0; i < bwt_size_; ++i) {
            const int32_t pos = sa[i];
            if (pos == 0) {
                bwt[i] = 0;
                primary_index_ = i;
            } else {
                bwt[i] = text[static_cast<size_t>(pos - 1)];
            }

            if (static_cast<size_t>(pos) % sample_rate_ == 0) {
                sampled_bv.set(i, true);
                sampled_sa_32_.push_back(static_cast<uint32_t>(pos));
                const size_t sample_idx = static_cast<size_t>(pos) / sample_rate_;
                if (sample_idx < num_inv_samples) {
                    inv_sampled_rows_[sample_idx] = i;
                }
            }
        }
    } else {
        std::vector<int64_t> s(bwt_size_);
        for (size_t i = 0; i < text_size_; ++i) {
            s[i] = static_cast<int64_t>(text[i]);
        }
        s[text_size_] = 0;

        auto sa = SaisBuilder<int64_t>::build(s, 258);

        sampled_sa_64_.clear();
        for (size_t i = 0; i < bwt_size_; ++i) {
            const int64_t pos = sa[i];
            if (pos == 0) {
                bwt[i] = 0;
                primary_index_ = i;
            } else {
                bwt[i] = text[static_cast<size_t>(pos - 1)];
            }

            if (static_cast<size_t>(pos) % sample_rate_ == 0) {
                sampled_bv.set(i, true);
                sampled_sa_64_.push_back(static_cast<uint64_t>(pos));
                const size_t sample_idx = static_cast<size_t>(pos) / sample_rate_;
                if (sample_idx < num_inv_samples) {
                    inv_sampled_rows_[sample_idx] = i;
                }
            }
        }
    }

    // 2. Compute C-table
    std::array<size_t, 256> counts{};
    counts.fill(0);
    for (uint8_t c : bwt) {
        counts[c]++;
    }

    size_t cumulative = 0;
    for (size_t i = 0; i < 256; ++i) {
        c_table_[i] = cumulative;
        cumulative += counts[i];
    }

    // 3. Build Wavelet Tree over BWT
    bwt_wt_ = WaveletTree(bwt);
    sampled_rows_bv_ = RankSelectBitVector(std::move(sampled_bv));
}

std::pair<size_t, size_t> FMIndex::backward_search(
    std::span<const uint8_t> pattern) const noexcept {
    if (pattern.empty() || text_size_ == 0) {
        return {0, 0};
    }

    size_t lo = 0;
    size_t hi = bwt_size_;

    for (size_t i = pattern.size(); i > 0; --i) {
        const uint8_t c = pattern[i - 1];
        if (c == 0) {
            return {0, 0};
        }

        lo = c_table_[c] + bwt_wt_.rank(c, lo);
        hi = c_table_[c] + bwt_wt_.rank(c, hi);

        if (lo >= hi) {
            return {0, 0};
        }
    }

    return {lo, hi};
}

std::pair<size_t, size_t> FMIndex::backward_search(std::string_view pattern) const noexcept {
    return backward_search(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pattern.data()), pattern.size()));
}

size_t FMIndex::count(std::span<const uint8_t> pattern) const noexcept {
    const auto [lo, hi] = backward_search(pattern);
    return hi - lo;
}

size_t FMIndex::count(std::string_view pattern) const noexcept {
    const auto [lo, hi] = backward_search(pattern);
    return hi - lo;
}

size_t FMIndex::locate_row(size_t r) const noexcept {
    size_t steps = 0;
    size_t curr_r = r;

    while (!sampled_rows_bv_.get(curr_r)) {
        curr_r = lf_map(curr_r);
        steps++;
    }

    const size_t sample_k = sampled_rows_bv_.rank1(curr_r + 1);
    size_t sa_val = 0;
    if (!is_64bit_) {
        sa_val = sampled_sa_32_[sample_k - 1];
    } else {
        sa_val = sampled_sa_64_[sample_k - 1];
    }

    return sa_val + steps;
}

std::vector<size_t> FMIndex::locate(std::span<const uint8_t> pattern) const {
    const auto [lo, hi] = backward_search(pattern);
    if (lo >= hi) {
        return {};
    }

    std::vector<size_t> matches;
    matches.reserve(hi - lo);

    for (size_t r = lo; r < hi; ++r) {
        const size_t pos = locate_row(r);
        if (pos < text_size_) {
            matches.push_back(pos);
        }
    }

    std::sort(matches.begin(), matches.end());
    return matches;
}

std::vector<size_t> FMIndex::locate(std::string_view pattern) const {
    return locate(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pattern.data()), pattern.size()));
}

std::string FMIndex::extract(size_t begin, size_t end) const {
    if (begin >= end || begin >= text_size_ || text_size_ == 0) {
        return "";
    }
    if (end > text_size_) {
        end = text_size_;
    }

    const size_t len = end - begin;
    std::string result(len, '\0');

    size_t anchor_pos = ((end + sample_rate_ - 1) / sample_rate_) * sample_rate_;
    size_t curr_row = 0;
    if (anchor_pos >= text_size_) {
        anchor_pos = text_size_;
        curr_row = 0;  // Sentinel suffix T[text_size_] is always at SA row 0
    } else {
        const size_t sample_idx = anchor_pos / sample_rate_;
        curr_row = inv_sampled_rows_[sample_idx];
    }

    size_t curr_pos = anchor_pos;

    while (curr_pos > begin) {
        const uint8_t c = bwt_wt_.access(curr_row);
        if (curr_pos <= end && curr_pos > begin) {
            result[curr_pos - 1 - begin] = static_cast<char>(c);
        }
        if (curr_pos - 1 == begin) {
            break;
        }
        curr_row = c_table_[c] + bwt_wt_.rank(c, curr_row);
        curr_pos--;
    }

    return result;
}

void FMIndex::serialize(std::ostream& os) const {
    const uint64_t ts = static_cast<uint64_t>(text_size_);
    const uint64_t bs = static_cast<uint64_t>(bwt_size_);
    const uint64_t sr = static_cast<uint64_t>(sample_rate_);
    const uint64_t pi = static_cast<uint64_t>(primary_index_);
    const uint8_t is64 = is_64bit_ ? 1 : 0;

    os.write(reinterpret_cast<const char*>(&ts), sizeof(ts));
    os.write(reinterpret_cast<const char*>(&bs), sizeof(bs));
    os.write(reinterpret_cast<const char*>(&sr), sizeof(sr));
    os.write(reinterpret_cast<const char*>(&pi), sizeof(pi));
    os.write(reinterpret_cast<const char*>(&is64), sizeof(is64));

    for (size_t i = 0; i < 256; ++i) {
        const uint64_t c = static_cast<uint64_t>(c_table_[i]);
        os.write(reinterpret_cast<const char*>(&c), sizeof(c));
    }

    bwt_wt_.serialize(os);
    sampled_rows_bv_.serialize(os);

    if (!is_64bit_) {
        const uint64_t s32_sz = static_cast<uint64_t>(sampled_sa_32_.size());
        os.write(reinterpret_cast<const char*>(&s32_sz), sizeof(s32_sz));
        if (s32_sz > 0) {
            os.write(reinterpret_cast<const char*>(sampled_sa_32_.data()),
                     static_cast<std::streamsize>(s32_sz * sizeof(uint32_t)));
        }
    } else {
        const uint64_t s64_sz = static_cast<uint64_t>(sampled_sa_64_.size());
        os.write(reinterpret_cast<const char*>(&s64_sz), sizeof(s64_sz));
        if (s64_sz > 0) {
            os.write(reinterpret_cast<const char*>(sampled_sa_64_.data()),
                     static_cast<std::streamsize>(s64_sz * sizeof(uint64_t)));
        }
    }

    const uint64_t inv_sz = static_cast<uint64_t>(inv_sampled_rows_.size());
    os.write(reinterpret_cast<const char*>(&inv_sz), sizeof(inv_sz));
    if (inv_sz > 0) {
        os.write(reinterpret_cast<const char*>(inv_sampled_rows_.data()),
                 static_cast<std::streamsize>(inv_sz * sizeof(size_t)));
    }
}

FMIndex FMIndex::deserialize(std::istream& is) {
    FMIndex fmi;
    uint64_t ts = 0;
    uint64_t bs = 0;
    uint64_t sr = 0;
    uint64_t pi = 0;
    uint8_t is64 = 0;

    if (!is.read(reinterpret_cast<char*>(&ts), sizeof(ts)) ||
        !is.read(reinterpret_cast<char*>(&bs), sizeof(bs)) ||
        !is.read(reinterpret_cast<char*>(&sr), sizeof(sr)) ||
        !is.read(reinterpret_cast<char*>(&pi), sizeof(pi)) ||
        !is.read(reinterpret_cast<char*>(&is64), sizeof(is64))) {
        throw std::runtime_error("Failed to read FMIndex header");
    }

    if (sr == 0) {
        throw std::runtime_error("Corrupted FMIndex: sample_rate is 0");
    }
    if (bs > 0 && pi >= bs) {
        throw std::runtime_error("Corrupted FMIndex: invalid primary_index");
    }

    fmi.text_size_ = static_cast<size_t>(ts);
    fmi.bwt_size_ = static_cast<size_t>(bs);
    fmi.sample_rate_ = static_cast<size_t>(sr);
    fmi.primary_index_ = static_cast<size_t>(pi);
    fmi.is_64bit_ = (is64 != 0);

    for (size_t i = 0; i < 256; ++i) {
        uint64_t c = 0;
        if (!is.read(reinterpret_cast<char*>(&c), sizeof(c))) {
            throw std::runtime_error("Failed to read FMIndex C-table");
        }
        fmi.c_table_[i] = static_cast<size_t>(c);
    }

    fmi.bwt_wt_ = WaveletTree::deserialize(is);
    fmi.sampled_rows_bv_ = RankSelectBitVector::deserialize(is);

    if (!fmi.is_64bit_) {
        uint64_t s32_sz = 0;
        if (!is.read(reinterpret_cast<char*>(&s32_sz), sizeof(s32_sz))) {
            throw std::runtime_error("Failed to read FMIndex sampled SA 32 size");
        }
        fmi.sampled_sa_32_.resize(static_cast<size_t>(s32_sz));
        if (s32_sz > 0) {
            if (!is.read(reinterpret_cast<char*>(fmi.sampled_sa_32_.data()),
                         static_cast<std::streamsize>(s32_sz * sizeof(uint32_t)))) {
                throw std::runtime_error("Failed to read FMIndex sampled SA 32 data");
            }
        }
    } else {
        uint64_t s64_sz = 0;
        if (!is.read(reinterpret_cast<char*>(&s64_sz), sizeof(s64_sz))) {
            throw std::runtime_error("Failed to read FMIndex sampled SA 64 size");
        }
        fmi.sampled_sa_64_.resize(static_cast<size_t>(s64_sz));
        if (s64_sz > 0) {
            if (!is.read(reinterpret_cast<char*>(fmi.sampled_sa_64_.data()),
                         static_cast<std::streamsize>(s64_sz * sizeof(uint64_t)))) {
                throw std::runtime_error("Failed to read FMIndex sampled SA 64 data");
            }
        }
    }

    uint64_t inv_sz = 0;
    if (!is.read(reinterpret_cast<char*>(&inv_sz), sizeof(inv_sz))) {
        throw std::runtime_error("Failed to read FMIndex inv_samples size");
    }
    fmi.inv_sampled_rows_.resize(static_cast<size_t>(inv_sz));
    if (inv_sz > 0) {
        if (!is.read(reinterpret_cast<char*>(fmi.inv_sampled_rows_.data()),
                     static_cast<std::streamsize>(inv_sz * sizeof(size_t)))) {
            throw std::runtime_error("Failed to read FMIndex inv_samples data");
        }
    }

    return fmi;
}

}  // namespace needlefish
