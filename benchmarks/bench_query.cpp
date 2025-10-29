// Query Pagination Benchmarks: Offset vs Cursor (anchor-based)

#include <benchmark/benchmark.h>
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "storage/base_entity.h"
#include "query/query_engine.h"

using themis::RocksDBWrapper;
using themis::SecondaryIndexManager;
using themis::BaseEntity;
using themis::QueryEngine;
using themis::ConjunctiveQuery;
using themis::OrderBy;

namespace {
struct BenchEnv {
    std::shared_ptr<RocksDBWrapper> storage;
    std::shared_ptr<SecondaryIndexManager> secIdx;
    bool ready = false;

    static BenchEnv& instance() {
        static BenchEnv env; return env;
    }

    static std::string padInt(int v, int width=6) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%0*d", width, v);
        return std::string(buf);
    }

    void initOnce(size_t N = 100000) {
        if (ready) return;
        const std::string db_path = "data/themis_bench_query";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
        RocksDBWrapper::Config cfg; cfg.db_path = db_path; cfg.memtable_size_mb = 128; cfg.block_cache_size_mb = 256;
        storage = std::make_shared<RocksDBWrapper>(cfg);
        if (!storage->open()) {
            throw std::runtime_error("Failed to open RocksDB for benchmark");
        }
        secIdx = std::make_shared<SecondaryIndexManager>(*storage);
        // Create range index for ORDER BY
        auto st = secIdx->createRangeIndex("bench_users", "age");
        if (!st.ok) throw std::runtime_error("Failed to create range index: " + st.message);

        // Populate N entities: ascending age with zero-padded strings
        std::vector<BaseEntity> batch; batch.reserve(1000);
        for (size_t i = 0; i < N; ++i) {
            std::string pk = std::string("u_") + padInt(static_cast<int>(i), 8);
            std::string age = padInt(static_cast<int>(i)); // 000000 .. 099999
            auto e = BaseEntity::fromFields(pk, BaseEntity::FieldMap{{"name", std::string("User ")+std::to_string(i)}, {"age", age}});
            auto pst = secIdx->put("bench_users", e);
            if (!pst.ok) throw std::runtime_error("Put failed at i=" + std::to_string(i));
        }
        ready = true;
    }
};
} // namespace

static void BM_Pagination_Offset(benchmark::State& state) {
    // Args: page_size, pages
    const int pageSize = static_cast<int>(state.range(0));
    const int pages = static_cast<int>(state.range(1));
    auto& env = BenchEnv::instance(); env.initOnce();
    QueryEngine engine(*env.storage, *env.secIdx);

    for (auto _ : state) {
        size_t totalFetched = 0;
        for (int p = 0; p < pages; ++p) {
            size_t offset = static_cast<size_t>(p) * static_cast<size_t>(pageSize);
            ConjunctiveQuery q; q.table = "bench_users";
            OrderBy ob; ob.column = "age"; ob.desc = false; ob.limit = static_cast<size_t>(pageSize) + offset;
            q.orderBy = ob;
            auto [st, ents] = engine.executeAndEntities(q);
            if (!st.ok) {
                state.SkipWithError(st.message.c_str());
                return;
            }
            // emulate HTTP post-fetch slicing of last page
            if (ents.size() > offset) {
                size_t last = std::min(ents.size(), offset + static_cast<size_t>(pageSize));
                totalFetched += (last - offset);
            }
        }
        state.counters["pages"] = pages;
        state.counters["page_size"] = pageSize;
        state.counters["fetched_items"] = static_cast<double>(totalFetched);
    }
}

static void BM_Pagination_Cursor(benchmark::State& state) {
    // Args: page_size, pages
    const int pageSize = static_cast<int>(state.range(0));
    const int pages = static_cast<int>(state.range(1));
    auto& env = BenchEnv::instance(); env.initOnce();
    QueryEngine engine(*env.storage, *env.secIdx);

    std::optional<std::string> anchorValue;
    std::optional<std::string> anchorPk;

    for (auto _ : state) {
        size_t totalFetched = 0;
        anchorValue.reset(); anchorPk.reset();
        for (int p = 0; p < pages; ++p) {
            ConjunctiveQuery q; q.table = "bench_users";
            OrderBy ob; ob.column = "age"; ob.desc = false; ob.limit = static_cast<size_t>(pageSize) + 1;
            ob.cursor_value = anchorValue; ob.cursor_pk = anchorPk; // first page: std::nullopt
            q.orderBy = ob;
            auto [st, ents] = engine.executeAndEntities(q);
            if (!st.ok) { state.SkipWithError(st.message.c_str()); return; }
            bool has_more = ents.size() > static_cast<size_t>(pageSize);
            size_t count = std::min(ents.size(), static_cast<size_t>(pageSize));
            totalFetched += count;
            if (count > 0) {
                const auto& last = ents[count - 1];
                anchorPk = last.getPrimaryKey();
                // Know the age because it is a field; extractField returns optional<string>
                auto v = last.extractField("age");
                if (v) anchorValue = *v; else anchorValue.reset();
            }
            if (!has_more) break;
        }
        state.counters["pages"] = pages;
        state.counters["page_size"] = pageSize;
        state.counters["fetched_items"] = static_cast<double>(totalFetched);
    }
}

// Register with typical settings: page_size=50, pages=50
BENCHMARK(BM_Pagination_Offset)->Args({50, 50})->Unit(benchmark::kMillisecond);
BENCHMARK(BM_Pagination_Cursor)->Args({50, 50})->Unit(benchmark::kMillisecond);
