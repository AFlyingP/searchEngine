#include "sa/sais.hpp"

#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace needlefish {

namespace {

constexpr uint8_t S_TYPE = 0;
constexpr uint8_t L_TYPE = 1;

template <typename IndexType>
std::vector<IndexType> sais_internal(std::span<const IndexType> s, size_t alphabet_size,
                                     SaisStats* stats, size_t depth) {
    const size_t n = s.size();
    if (n == 0) {
        return {};
    }
    if (n == 1) {
        return {0};
    }
    if (n == 2) {
        if (s[0] < s[1]) {
            return {0, 1};
        } else {
            return {1, 0};
        }
    }

    if (stats) {
        stats->recursion_depth = std::max(stats->recursion_depth, depth);
        // Estimate memory usage for this level:
        // t array: n bytes
        // SA array: n * sizeof(IndexType)
        // buckets: alphabet_size * sizeof(size_t) * 2
        // lms positions & aux: ~n * sizeof(IndexType)
        const size_t level_mem =
            n * sizeof(uint8_t) + n * sizeof(IndexType) * 2 + alphabet_size * sizeof(size_t) * 2;
        stats->peak_memory_bytes += level_mem;
    }

    // 1. Classify S/L types
    std::vector<uint8_t> t(n);
    t[n - 1] = S_TYPE;
    for (size_t i = n - 1; i > 0; --i) {
        const size_t prev = i - 1;
        if (s[prev] < s[i]) {
            t[prev] = S_TYPE;
        } else if (s[prev] > s[i]) {
            t[prev] = L_TYPE;
        } else {
            t[prev] = t[i];
        }
    }

    auto is_lms = [&](size_t i) -> bool { return i > 0 && t[i] == S_TYPE && t[i - 1] == L_TYPE; };

    // 2. Count bucket sizes and compute bucket heads and tails
    std::vector<size_t> bucket_counts(alphabet_size, 0);
    for (size_t i = 0; i < n; ++i) {
        bucket_counts[static_cast<size_t>(s[i])]++;
    }

    std::vector<size_t> bucket_heads(alphabet_size, 0);
    std::vector<size_t> bucket_tails(alphabet_size, 0);
    size_t cumulative = 0;
    for (size_t c = 0; c < alphabet_size; ++c) {
        bucket_heads[c] = cumulative;
        cumulative += bucket_counts[c];
        bucket_tails[c] = (bucket_counts[c] > 0) ? (cumulative - 1) : cumulative;
    }

    // Collect LMS positions
    std::vector<IndexType> lms_positions;
    for (size_t i = 1; i < n; ++i) {
        if (is_lms(i)) {
            lms_positions.push_back(static_cast<IndexType>(i));
        }
    }
    const size_t num_lms = lms_positions.size();

    // 3. Induced sort LMS substrings
    std::vector<IndexType> sa(n, -1);
    std::vector<size_t> b_tails = bucket_tails;
    for (IndexType pos : lms_positions) {
        const size_t c = static_cast<size_t>(s[static_cast<size_t>(pos)]);
        sa[b_tails[c]--] = pos;
    }

    // Induce L-type
    std::vector<size_t> b_heads = bucket_heads;
    for (size_t i = 0; i < n; ++i) {
        if (sa[i] > 0) {
            const size_t j = static_cast<size_t>(sa[i] - 1);
            if (t[j] == L_TYPE) {
                const size_t c = static_cast<size_t>(s[j]);
                sa[b_heads[c]++] = static_cast<IndexType>(j);
            }
        }
    }

    // Induce S-type
    b_tails = bucket_tails;
    for (size_t i = n; i > 0; --i) {
        const size_t idx = i - 1;
        if (sa[idx] > 0) {
            const size_t j = static_cast<size_t>(sa[idx] - 1);
            if (t[j] == S_TYPE) {
                const size_t c = static_cast<size_t>(s[j]);
                sa[b_tails[c]--] = static_cast<IndexType>(j);
            }
        }
    }

    // 4. Name sorted LMS substrings
    std::vector<IndexType> sorted_lms;
    sorted_lms.reserve(num_lms);
    for (size_t i = 0; i < n; ++i) {
        if (sa[i] >= 0 && is_lms(static_cast<size_t>(sa[i]))) {
            sorted_lms.push_back(sa[i]);
        }
    }

    auto lms_equal = [&](IndexType p, IndexType q) -> bool {
        if (p == q) {
            return true;
        }
        for (size_t d = 0;; ++d) {
            const size_t pd = static_cast<size_t>(p) + d;
            const size_t qd = static_cast<size_t>(q) + d;
            if (pd >= n || qd >= n) {
                return false;
            }
            if (s[pd] != s[qd] || t[pd] != t[qd]) {
                return false;
            }
            const bool pd_lms = is_lms(pd);
            const bool qd_lms = is_lms(qd);
            if (pd_lms != qd_lms) {
                return false;
            }
            if (d > 0 && pd_lms && qd_lms) {
                return true;
            }
        }
    };

    std::vector<IndexType> lms_names(n, -1);
    IndexType current_name = 0;
    if (!sorted_lms.empty()) {
        lms_names[static_cast<size_t>(sorted_lms[0])] = current_name;
        for (size_t i = 1; i < sorted_lms.size(); ++i) {
            if (!lms_equal(sorted_lms[i - 1], sorted_lms[i])) {
                current_name++;
            }
            lms_names[static_cast<size_t>(sorted_lms[i])] = current_name;
        }
    }

    std::vector<IndexType> s1;
    s1.reserve(num_lms);
    for (IndexType pos : lms_positions) {
        s1.push_back(lms_names[static_cast<size_t>(pos)]);
    }

    const size_t alphabet_size1 = static_cast<size_t>(current_name + 1);

    // 5. Recursive call or direct compute
    std::vector<IndexType> sa1;
    if (alphabet_size1 < num_lms) {
        sa1 = sais_internal<IndexType>(s1, alphabet_size1, stats, depth + 1);
    } else {
        sa1.resize(num_lms);
        for (size_t i = 0; i < num_lms; ++i) {
            sa1[static_cast<size_t>(s1[i])] = static_cast<IndexType>(i);
        }
    }

    // 6. Induce final SA from SA1
    std::fill(sa.begin(), sa.end(), -1);
    b_tails = bucket_tails;
    for (size_t i = num_lms; i > 0; --i) {
        const size_t lms_idx = static_cast<size_t>(sa1[i - 1]);
        const IndexType pos = lms_positions[lms_idx];
        const size_t c = static_cast<size_t>(s[static_cast<size_t>(pos)]);
        sa[b_tails[c]--] = pos;
    }

    // Induce L-type
    b_heads = bucket_heads;
    for (size_t i = 0; i < n; ++i) {
        if (sa[i] > 0) {
            const size_t j = static_cast<size_t>(sa[i] - 1);
            if (t[j] == L_TYPE) {
                const size_t c = static_cast<size_t>(s[j]);
                sa[b_heads[c]++] = static_cast<IndexType>(j);
            }
        }
    }

    // Induce S-type
    b_tails = bucket_tails;
    for (size_t i = n; i > 0; --i) {
        const size_t idx = i - 1;
        if (sa[idx] > 0) {
            const size_t j = static_cast<size_t>(sa[idx] - 1);
            if (t[j] == S_TYPE) {
                const size_t c = static_cast<size_t>(s[j]);
                sa[b_tails[c]--] = static_cast<IndexType>(j);
            }
        }
    }

    return sa;
}

}  // namespace

template <typename IndexType>
std::vector<IndexType> SaisBuilder<IndexType>::build(std::span<const IndexType> s,
                                                     size_t alphabet_size, SaisStats* stats) {
    if (stats) {
        stats->text_length = s.size();
        stats->peak_memory_bytes = 0;
        stats->recursion_depth = 0;
    }
    return sais_internal<IndexType>(s, alphabet_size, stats, 1);
}

template class SaisBuilder<int32_t>;
template class SaisBuilder<int64_t>;

SuffixArray build_suffix_array(std::span<const uint8_t> text, SaisStats* stats) {
    const size_t n = text.size();
    if (n == 0) {
        return SuffixArray(std::vector<int32_t>{});
    }

    // Check if 32-bit or 64-bit SA is required
    const bool use_64bit = (n + 1) >= static_cast<size_t>(std::numeric_limits<int32_t>::max());

    if (!use_64bit) {
        // Build integer sequence s with sentinel 0 appended:
        // text bytes 0..255 mapped to 1..256, sentinel = 0.
        std::vector<int32_t> s(n + 1);
        for (size_t i = 0; i < n; ++i) {
            s[i] = static_cast<int32_t>(text[i]) + 1;
        }
        s[n] = 0;

        auto full_sa = SaisBuilder<int32_t>::build(s, 258, stats);

        // full_sa[0] is the sentinel suffix at position n.
        // Strip the sentinel suffix to get the SA of the original text.
        std::vector<int32_t> sa(n);
        for (size_t i = 0; i < n; ++i) {
            sa[i] = full_sa[i + 1];
        }
        return SuffixArray(std::move(sa));
    } else {
        std::vector<int64_t> s(n + 1);
        for (size_t i = 0; i < n; ++i) {
            s[i] = static_cast<int64_t>(text[i]) + 1;
        }
        s[n] = 0;

        auto full_sa = SaisBuilder<int64_t>::build(s, 258, stats);

        std::vector<int64_t> sa(n);
        for (size_t i = 0; i < n; ++i) {
            sa[i] = full_sa[i + 1];
        }
        return SuffixArray(std::move(sa));
    }
}

SuffixArray build_suffix_array(std::string_view text, SaisStats* stats) {
    return build_suffix_array(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()),
        stats);
}

}  // namespace needlefish
