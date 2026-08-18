#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "util/analyzer.hpp"

namespace needlefish {

/**
 * @brief Best-window snippet generator with query term highlighting.
 */
class SnippetGenerator {
  public:
    static constexpr size_t DEFAULT_MAX_SNIPPET_LEN = 160;

    explicit SnippetGenerator(size_t max_snippet_len = DEFAULT_MAX_SNIPPET_LEN,
                              std::string_view pre_tag = "<em>",
                              std::string_view post_tag = "</em>");

    /**
     * @brief Generate highlighted HTML snippet from full document text for a query.
     */
    [[nodiscard]] std::string highlight(std::string_view doc_text,
                                        std::span<const std::string> query_terms) const;

  private:
    size_t max_snippet_len_{DEFAULT_MAX_SNIPPET_LEN};
    std::string pre_tag_{"<em>"};
    std::string post_tag_{"</em>"};
    Analyzer analyzer_{true, true};
};

}  // namespace needlefish
