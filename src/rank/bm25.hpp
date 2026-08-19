#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace needlefish {

/**
 * @brief Okapi BM25 Ranking Function.
 * Default parameters: k1 = 0.9, b = 0.4.
 */
class BM25Scorer {
  public:
    static constexpr double DEFAULT_K1 = 0.9;
    static constexpr double DEFAULT_B = 0.4;

    explicit BM25Scorer(double k1 = DEFAULT_K1, double b = DEFAULT_B) : k1_(k1), b_(b) {}

    [[nodiscard]] static double compute_idf(size_t doc_freq, size_t total_docs) noexcept {
        if (total_docs == 0)
            return 0.0;
        const double n = static_cast<double>(doc_freq);
        const double N = static_cast<double>(total_docs);
        return std::log(1.0 + (N - n + 0.5) / (n + 0.5));
    }

    [[nodiscard]] float score(uint32_t term_freq, uint32_t doc_len, double avg_doc_len,
                              double idf) const noexcept {
        const double tf = static_cast<double>(term_freq);
        const double len = static_cast<double>(doc_len);
        const double denom =
            tf + k1_ * (1.0 - b_ + b_ * (len / (avg_doc_len > 0 ? avg_doc_len : 1.0)));
        const double tf_component = (tf * (k1_ + 1.0)) / (denom > 0 ? denom : 1.0);
        return static_cast<float>(idf * tf_component);
    }

    [[nodiscard]] static float score_tf(uint32_t term_freq, uint32_t doc_len, double avg_doc_len,
                                        double idf, double k1 = DEFAULT_K1,
                                        double b = DEFAULT_B) noexcept {
        const double tf = static_cast<double>(term_freq);
        const double len = static_cast<double>(doc_len);
        const double denom =
            tf + k1 * (1.0 - b + b * (len / (avg_doc_len > 0 ? avg_doc_len : 1.0)));
        const double tf_component = (tf * (k1 + 1.0)) / (denom > 0 ? denom : 1.0);
        return static_cast<float>(idf * tf_component);
    }

    [[nodiscard]] double k1() const noexcept { return k1_; }
    [[nodiscard]] double b() const noexcept { return b_; }

  private:
    double k1_{DEFAULT_K1};
    double b_{DEFAULT_B};
};

}  // namespace needlefish
