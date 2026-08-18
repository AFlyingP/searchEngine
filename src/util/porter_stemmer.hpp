#pragma once

#include <string>
#include <string_view>

namespace needlefish {

/**
 * @brief Hand-written implementation of the Martin Porter (1980) Stemming Algorithm.
 * Strictly compliant with official Porter stemmer test vectors.
 */
class PorterStemmer {
  public:
    /**
     * @brief Stem a lowercase ASCII word in-place or returning a new string.
     */
    static std::string stem(std::string_view word);
};

}  // namespace needlefish
