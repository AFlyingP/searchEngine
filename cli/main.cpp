#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "automata/autocomplete.hpp"
#include "invidx/index_builder.hpp"
#include "rank/hybrid_search.hpp"
#include "rank/query_eval.hpp"
#include "rank/snippet.hpp"
#include "store/index_file.hpp"

using namespace needlefish;

void print_usage() {
    std::cout << "needlefish v1.0.0 - high-performance full-text search engine\n\n";
    std::cout << "Usage: needlefish <command> [options]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  index    Build an index from a JSONL file\n";
    std::cout
        << "           Options: --input <file.jsonl> --output <index.idx> [--enable-substring | --enable-fm]\n\n";
    std::cout << "  search   Search an existing index\n";
    std::cout << "           Options: --index <index.idx> --query \"<query>\" [--k <num>] [--fuzzy "
                 "<dist>] [--regex] [--substring]\n\n";
    std::cout << "  suggest  Autocomplete and fuzzy suggest terms from the index\n";
    std::cout << "           Options: --index <index.idx> --query \"<prefix|term>\" [--fuzzy] "
                 "[--max-dist <num>]\n\n";
    std::cout << "  stats    Display statistics for an index file\n";
    std::cout << "           Options: --index <index.idx>\n";
}

int cmd_index(int argc, char* argv[]) {
    std::string input_path;
    std::string output_path = "corpus.idx";
    bool enable_substring = false;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--input" || arg == "-i") {
            if (i + 1 < argc)
                input_path = argv[++i];
        } else if (arg == "--output" || arg == "-o") {
            if (i + 1 < argc)
                output_path = argv[++i];
        } else if (arg == "--enable-substring" || arg == "-s" || arg == "--enable-fm") {
            enable_substring = true;
        }
    }

    if (input_path.empty()) {
        std::cerr << "Error: Missing required argument --input <file.jsonl>\n";
        return 1;
    }

    if (!std::filesystem::exists(input_path)) {
        std::cerr << "Error: Input file does not exist: " << input_path << "\n";
        return 1;
    }

    std::cout << "Indexing: " << input_path << " -> " << output_path
              << (enable_substring ? " (with FM-Index substring support)" : "") << "\n";
    const auto t0 = std::chrono::high_resolution_clock::now();

    IndexBuilder builder;
    builder.set_enable_fm_index(enable_substring);
    size_t docs_indexed = builder.index_jsonl_file(input_path);
    builder.write_index(output_path);

    const auto t1 = std::chrono::high_resolution_clock::now();
    const double elapsed_ms =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count() / 1000.0;

    const auto file_size = std::filesystem::file_size(output_path);

    std::cout << "Successfully indexed " << docs_indexed << " documents (" << builder.total_terms()
              << " unique terms) in " << std::fixed << std::setprecision(2) << elapsed_ms
              << " ms\n";
    std::cout << "Output index file size: " << file_size << " bytes (" << std::fixed
              << std::setprecision(2) << (file_size / 1024.0 / 1024.0) << " MB)\n";
    return 0;
}

int cmd_search(int argc, char* argv[]) {
    std::string index_path;
    std::string query_str;
    size_t top_k = 10;
    size_t fuzzy_dist = 0;
    bool force_regex = false;
    bool force_substring = false;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--index" || arg == "-i") {
            if (i + 1 < argc)
                index_path = argv[++i];
        } else if (arg == "--query" || arg == "-q") {
            if (i + 1 < argc)
                query_str = argv[++i];
        } else if (arg == "--k" || arg == "-k") {
            if (i + 1 < argc) {
                try {
                    long long parsed = std::stoll(argv[++i]);
                    top_k = static_cast<size_t>(std::clamp<long long>(parsed, 1, 1000));
                } catch (...) {
                    top_k = 10;
                }
            }
        } else if (arg == "--fuzzy" || arg == "-f") {
            fuzzy_dist = 2;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                try {
                    long long parsed = std::stoll(argv[++i]);
                    fuzzy_dist = static_cast<size_t>(std::clamp<long long>(parsed, 0, 2));
                } catch (...) {
                    fuzzy_dist = 2;
                }
            }
        } else if (arg == "--regex" || arg == "-r") {
            force_regex = true;
        } else if (arg == "--substring" || arg == "-s") {
            force_substring = true;
        }
    }

    if (index_path.empty()) {
        std::cerr << "Error: Missing required argument --index <index.idx>\n";
        return 1;
    }
    if (query_str.empty()) {
        std::cerr << "Error: Missing required argument --query \"<text>\"\n";
        return 1;
    }

    if (!std::filesystem::exists(index_path)) {
        std::cerr << "Error: Index file does not exist: " << index_path << "\n";
        return 1;
    }

    IndexView index(index_path);
    HybridSearchEngine hybrid(index);
    SnippetGenerator snippet_gen;

    HybridSearchResult result;
    if (force_regex) {
        result = hybrid.search_regex(query_str, top_k);
    } else if (force_substring) {
        result = hybrid.search_substring(query_str, top_k);
    } else if (fuzzy_dist > 0) {
        result = hybrid.search_fuzzy(query_str, fuzzy_dist, top_k);
    } else {
        result = hybrid.search(query_str, top_k);
    }

    std::cout << "Search results for: \"" << query_str << "\" (" << result.hits.size()
              << " matches in " << result.took_us << " µs):\n\n";

    if (result.hits.empty()) {
        std::cout << "  No documents matched the query.\n";
        if (!result.correction_suggestion.empty()) {
            std::cout << "  Did you mean: \"" << result.correction_suggestion << "\"?\n";
        }
        return 0;
    }

    Analyzer analyzer;
    auto query_tokens = analyzer.analyze(query_str);
    std::vector<std::string> qterms;
    for (const auto& tok : query_tokens) {
        qterms.push_back(tok.term);
    }

    for (size_t rank = 0; rank < result.hits.size(); ++rank) {
        const auto& hit = result.hits[rank];
        std::string_view title = index.doc_title(hit.doc_id);
        std::string_view text = index.doc_text(hit.doc_id);
        std::string snippet = snippet_gen.highlight(text, qterms);

        std::cout << "  [" << (rank + 1) << "] (Score: " << std::fixed << std::setprecision(4)
                  << hit.score << ", DocID: " << index.external_id(hit.doc_id) << ")\n";
        std::cout << "      Title:   " << (title.empty() ? "(Untitled)" : title) << "\n";
        std::cout << "      Snippet: " << snippet << "\n\n";
    }

    return 0;
}

int cmd_suggest(int argc, char* argv[]) {
    std::string index_path;
    std::string query_str;
    bool is_fuzzy = false;
    size_t max_dist = 2;
    size_t top_k = 10;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--index" || arg == "-i") {
            if (i + 1 < argc)
                index_path = argv[++i];
        } else if (arg == "--query" || arg == "-q") {
            if (i + 1 < argc)
                query_str = argv[++i];
        } else if (arg == "--fuzzy" || arg == "-f") {
            is_fuzzy = true;
        } else if (arg == "--max-dist" || arg == "-d") {
            if (i + 1 < argc) {
                try {
                    long long parsed = std::stoll(argv[++i]);
                    max_dist = static_cast<size_t>(std::clamp<long long>(parsed, 0, 2));
                } catch (...) {
                    max_dist = 2;
                }
            }
        } else if (arg == "--k" || arg == "-k") {
            if (i + 1 < argc) {
                try {
                    long long parsed = std::stoll(argv[++i]);
                    top_k = static_cast<size_t>(std::clamp<long long>(parsed, 1, 100));
                } catch (...) {
                    top_k = 10;
                }
            }
        }
    }

    if (index_path.empty()) {
        std::cerr << "Error: Missing required argument --index <index.idx>\n";
        return 1;
    }
    if (query_str.empty()) {
        std::cerr << "Error: Missing required argument --query \"<prefix>\"\n";
        return 1;
    }

    if (!std::filesystem::exists(index_path)) {
        std::cerr << "Error: Index file does not exist: " << index_path << "\n";
        return 1;
    }

    IndexView index(index_path);
    AutocompleteEngine autocomplete(index);

    std::vector<Suggestion> suggestions;
    if (is_fuzzy) {
        suggestions = autocomplete.fuzzy_suggest(query_str, max_dist, top_k);
    } else {
        suggestions = autocomplete.prefix_suggest(query_str, top_k);
    }

    std::cout << "Suggestions for \"" << query_str << "\" (" << (is_fuzzy ? "fuzzy" : "prefix")
              << ", " << suggestions.size() << " results):\n\n";

    for (size_t i = 0; i < suggestions.size(); ++i) {
        const auto& s = suggestions[i];
        std::cout << "  [" << (i + 1) << "] " << s.text << " (doc_freq: " << s.doc_freq
                  << ", edit_dist: " << s.edit_distance << ")\n";
    }

    return 0;
}

int cmd_stats(int argc, char* argv[]) {
    std::string index_path;

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--index" || arg == "-i") {
            if (i + 1 < argc)
                index_path = argv[++i];
        }
    }

    if (index_path.empty()) {
        std::cerr << "Error: Missing required argument --index <index.idx>\n";
        return 1;
    }

    if (!std::filesystem::exists(index_path)) {
        std::cerr << "Error: Index file does not exist: " << index_path << "\n";
        return 1;
    }

    IndexView index(index_path);
    const auto& stats = index.stats();
    const auto file_size = std::filesystem::file_size(index_path);

    std::cout << "Index Statistics (" << index_path << "):\n";
    std::cout << "  Total Documents:       " << stats.total_docs << "\n";
    std::cout << "  Total Indexed Tokens:  " << stats.total_tokens << "\n";
    std::cout << "  Average Doc Length:    " << std::fixed << std::setprecision(2)
              << stats.avg_doc_len << " tokens\n";
    std::cout << "  Unique Terms in Trie:  " << index.term_dict().num_terms() << "\n";
    std::cout << "  Trie Flat Nodes:       " << index.term_dict().num_nodes() << "\n";
    std::cout << "  Has FM-Index Substring:" << (index.has_fm_index() ? " Yes" : " No") << "\n";
    std::cout << "  Total Index File Size: " << file_size << " bytes ("
              << (file_size / 1024.0 / 1024.0) << " MB)\n";
    return 0;
}

#include "server/http_server.hpp"

int cmd_serve(int argc, char* argv[]) {
    std::string index_path;
    std::string host = "127.0.0.1";
    uint16_t port = 8080;
    std::string web_dir = "web";

    for (int i = 2; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--index" || arg == "-i") {
            if (i + 1 < argc)
                index_path = argv[++i];
        } else if (arg == "--host" || arg == "-h") {
            if (i + 1 < argc)
                host = argv[++i];
        } else if (arg == "--port" || arg == "-p") {
            if (i + 1 < argc) {
                try {
                    long long parsed = std::stoll(argv[++i]);
                    port = static_cast<uint16_t>(std::clamp<long long>(parsed, 1, 65535));
                } catch (...) {
                    port = 8080;
                }
            }
        } else if (arg == "--web-dir" || arg == "-w") {
            if (i + 1 < argc)
                web_dir = argv[++i];
        }
    }

    if (index_path.empty()) {
        std::cerr << "Error: Missing required argument --index <index.idx>\n";
        return 1;
    }

    if (!std::filesystem::exists(index_path)) {
        std::cerr << "Error: Index file does not exist: " << index_path << "\n";
        return 1;
    }

    IndexView index(index_path);
    HttpServer server(index, host, port);
    server.set_static_directory(web_dir);
    server.start();

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 0;
    }

    try {
        std::string_view cmd = argv[1];
        if (cmd == "index") {
            return cmd_index(argc, argv);
        } else if (cmd == "search") {
            return cmd_search(argc, argv);
        } else if (cmd == "suggest") {
            return cmd_suggest(argc, argv);
        } else if (cmd == "stats") {
            return cmd_stats(argc, argv);
        } else if (cmd == "serve") {
            return cmd_serve(argc, argv);
        } else if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            print_usage();
            return 0;
        } else {
            std::cerr << "Unknown command: " << cmd << "\n\n";
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
