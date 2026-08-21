#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "invidx/index_builder.hpp"
#include "rank/query_eval.hpp"
#include "store/index_file.hpp"

using namespace needlefish;

class GoldenQuerySuiteTest : public ::testing::Test {
  protected:
    void SetUp() override {
        temp_dir_ = std::filesystem::temp_directory_path() / "needlefish_golden_idx";
        std::filesystem::create_directories(temp_dir_);
        idx_path_ = temp_dir_ / "golden_corpus.idx";

        // Build index with 25 distinct technical documents
        IndexBuilder builder;
        const std::vector<std::pair<std::string, std::string>> docs = {
            {"Linux Kernel Architecture",
             "The Linux kernel is a free and open-source monolithic Unix-like operating system "
             "kernel."},
            {"Memory Management in Operating Systems",
             "Virtual memory management uses paging and translation lookaside buffers TLB."},
            {"B-Tree and LSM-Tree Storage Engines",
             "Log-Structured Merge-trees LSM optimize write throughput while B-trees offer low "
             "latency reads."},
            {"Burrows-Wheeler Transform in Bio-informatics",
             "The BWT transform enables fast FM-index compressed pattern matching in genomic "
             "sequences."},
            {"Suffix Automaton and Suffix Trees",
             "Suffix automata provide minimal deterministic finite state representations of all "
             "substrings."},
            {"Wavelet Trees and Succinct Data Structures",
             "Wavelet tree data structures enable rank select queries over large alphabets in "
             "succinct space."},
            {"Vector Search and Approximate Nearest Neighbors",
             "Hierarchical Navigable Small World HNSW graphs provide fast approximate vector "
             "search."},
            {"BM25 Ranking Function",
             "Okapi BM25 ranks a set of documents based on the query terms appearing in each "
             "document."},
            {"Block-Max WAND Algorithm",
             "Block-Max WAND dynamically prunes posting lists to speed up top-k disjunctive search "
             "queries."},
            {"SIMD Vectorization with AVX2 and AVX-512",
             "SIMD vector instructions enable parallel data processing over 256-bit and 512-bit "
             "registers."},
            {"Inverted Index Compression Techniques",
             "Frame-of-reference bit-packing and Elias-Fano encoding achieve high compression "
             "ratios for docIDs."},
            {"Lock-Free Data Structures in Modern C++",
             "Atomic operations and memory order acquire release semantics avoid mutex "
             "contention."},
            {"Garbage Collection Algorithms",
             "Generational and concurrent mark-sweep garbage collectors minimize stop-the-world "
             "pauses."},
            {"Raft Distributed Consensus",
             "The Raft consensus algorithm achieves leader election, log replication, and safety "
             "across clusters."},
            {"TCP Congestion Control Algorithms",
             "BBR and CUBIC congestion control algorithms estimate bottleneck bandwidth and round "
             "trip time."},
            {"GPU Computing with CUDA",
             "CUDA kernels execute thousands of parallel threads across streaming "
             "multiprocessors."},
            {"Compilers and Abstract Syntax Trees",
             "Lexical analysis and parsing generate abstract syntax trees AST for intermediate "
             "representation."},
            {"Database Indexing Strategies",
             "Clustered indexes and secondary bitmap indexes optimize relational SQL query "
             "execution plans."},
            {"Distributed Key-Value Stores",
             "Consistent hashing partitions keys across distributed nodes with quorum "
             "replication."},
            {"Information Retrieval Metrics MAP and NDCG",
             "Mean Average Precision MAP and Normalized Discounted Cumulative Gain NDCG evaluate "
             "search ranking."},
            {"Asynchronous IO with io_uring in Linux",
             "The io_uring Linux interface enables zero-copy asynchronous system calls with "
             "submission queues."},
            {"Bloom Filters and Counting Filters",
             "Probabilistic Bloom filters test set membership with zero false negatives and "
             "tunable false positives."},
            {"Cache Replacement Policies LRU and ARC",
             "Adaptive Replacement Cache ARC balances recency and frequency to outperform Least "
             "Recently Used LRU."},
            {"Neural Network Quantization",
             "Post-training quantization converts 32-bit floating point weights into 8-bit "
             "integers without accuracy loss."},
            {"Fast Fourier Transform FFT",
             "The Cooley-Tukey Fast Fourier Transform algorithm computes discrete Fourier "
             "transforms in O(N log N) time."}};

        for (size_t i = 0; i < docs.size(); ++i) {
            builder.add_document(static_cast<uint32_t>(i), docs[i].first, docs[i].second);
        }

        builder.write_index(idx_path_);
        index_.open(idx_path_);
    }

    void TearDown() override {
        index_ = IndexView{};
        std::error_code ec;
        std::filesystem::remove_all(temp_dir_, ec);
    }

    std::filesystem::path temp_dir_;
    std::filesystem::path idx_path_;
    IndexView index_;
};

TEST_F(GoldenQuerySuiteTest, TwentyQueriesGoldenRun) {
    QueryEvaluator evaluator(index_);

    const std::vector<std::string> queries = {"linux kernel",
                                              "memory management",
                                              "b-tree lsm",
                                              "suffix automata",
                                              "wavelet tree",
                                              "vector search",
                                              "bm25 ranking",
                                              "block-max wand",
                                              "simd vectorization",
                                              "inverted index",
                                              "lock-free atomic",
                                              "garbage collection",
                                              "raft consensus",
                                              "tcp congestion",
                                              "cuda gpu",
                                              "compiler syntax tree",
                                              "database index",
                                              "distributed key-value",
                                              "information retrieval",
                                              "io_uring linux"};

    EXPECT_EQ(queries.size(), 20);

    for (const auto& q : queries) {
        auto result = evaluator.search(q, 10);
        EXPECT_GT(result.hits.size(), 0) << "Query returned 0 hits: " << q;
        // Verify scores are non-negative and strictly non-increasing
        for (size_t i = 0; i < result.hits.size(); ++i) {
            EXPECT_GT(result.hits[i].score, 0.0f);
            if (i > 0) {
                EXPECT_LE(result.hits[i].score, result.hits[i - 1].score);
            }
        }
    }

    // Negation test
    auto res_all_linux = evaluator.search("linux", 10);
    auto res_linux_no_kernel = evaluator.search("linux -kernel", 10);
    EXPECT_GT(res_all_linux.hits.size(), res_linux_no_kernel.hits.size());

    // OR test
    auto res_or = evaluator.search("cuda OR io_uring", 10);
    EXPECT_GE(res_or.hits.size(), 2u);
}
