#include <gtest/gtest.h>
#include "invidx/index_builder.hpp"
#include "store/index_file.hpp"
#include "rank/query_eval.hpp"
#include "automata/levenshtein.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

class ConcurrencyTest : public ::testing::Test {
protected:
    std::string test_jsonl = "concurrency_test_corpus.jsonl";
    std::string test_idx = "concurrency_test.idx";

    void SetUp() override {
        std::ofstream out(test_jsonl);
        for (int i = 1; i <= 50; ++i) {
            out << "{\"id\": " << i << ", \"title\": \"Doc " << i 
                << "\", \"text\": \"distributed systems search engine query evaluation algorithms concurrency threads lock free memory mapped\"}\n";
        }
        out.close();

        needlefish::IndexBuilder builder;
        builder.index_jsonl_file(test_jsonl);
        builder.write_index(test_idx);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(test_jsonl, ec);
        std::filesystem::remove(test_idx, ec);
    }
};

TEST_F(ConcurrencyTest, MultiThreadedConcurrentQueries) {
    needlefish::IndexView index;
    ASSERT_NO_THROW(index.open(test_idx));

    const int num_threads = 16;
    const int queries_per_thread = 500;
    std::atomic<uint64_t> total_hits_counter{0};
    std::vector<std::thread> workers;
    workers.reserve(num_threads);

    for (int t = 0; t < num_threads; ++t) {
        workers.emplace_back([&index, &total_hits_counter, queries_per_thread, t]() {
            std::vector<std::string> query_pool = {
                "distributed systems",
                "search engine",
                "lock free",
                "concurrency",
                "algorithms",
                "evaluation",
                "distributd~1",
                "nonexistentqueryterm"
            };

            for (int q = 0; q < queries_per_thread; ++q) {
                const auto& q_str = query_pool[static_cast<size_t>(q + t) % query_pool.size()];
                
                // 1. BM25 Query Evaluation
                needlefish::QueryEvaluator eval(index);
                auto results = eval.search(q_str, 10);
                total_hits_counter.fetch_add(results.hits.size(), std::memory_order_relaxed);

                // 2. Trie lookup
                auto exact_term = index.term_dict().lookup("search");
                if (exact_term.valid()) {
                    total_hits_counter.fetch_add(1, std::memory_order_relaxed);
                }

                // 3. Stats access
                const auto& stats = index.stats();
                EXPECT_EQ(stats.total_docs, 50u);
            }
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    EXPECT_GT(total_hits_counter.load(), 0u);
}
