#include <benchmark/benchmark.h>
#include "storage/base_entity.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include <random>
#include <filesystem>
#include <iostream>

namespace {
    constexpr size_t NUM_ENTITIES = 100'000;

    std::string makeRandomString(size_t len) {
        static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
        static std::mt19937 rng{std::random_device{}()};
        static std::uniform_int_distribution<size_t> dist(0, sizeof(charset) - 2);
        std::string s;
        s.reserve(len);
        for (size_t i = 0; i < len; ++i) s += charset[dist(rng)];
        return s;
    }

    int64_t randomInt(int64_t min, int64_t max) {
        static std::mt19937 rng{std::random_device{}()};
        std::uniform_int_distribution<int64_t> dist(min, max);
        return dist(rng);
    }

    void cleanupTestDB(const std::string& path) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
}

class RebuildFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State&) override {
        if (setup_done_) return;

        db_path_ = "bench_rebuild_db";
        cleanupTestDB(db_path_);

        themis::RocksDBWrapper::Config config;
        config.db_path = db_path_;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        config.block_cache_size_mb = 512;
        config.memtable_size_mb = 256;
        config.max_write_buffer_number = 4;

        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        secondary_ = std::make_unique<themis::SecondaryIndexManager>(*db_);

        std::cout << "Creating " << NUM_ENTITIES << " entities with 7 index types...\n";

        // Alle 7 Index-Typen
        secondary_->createIndex("Person", "email", true);
        secondary_->createCompositeIndex("Person", {"city", "age"}, false);
        secondary_->createRangeIndex("Person", "salary");
        secondary_->createSparseIndex("Person", "nickname", false);
        secondary_->createGeoIndex("Person", "location");
        secondary_->createTTLIndex("Person", "expires_at", 3600);
        secondary_->createFulltextIndex("Person", "bio");

        // NUM_ENTITIES erstellen
        for (size_t i = 0; i < NUM_ENTITIES; ++i) {
            themis::BaseEntity entity("person_" + std::to_string(i));
            entity.setField("email", "user" + std::to_string(i) + "@example.com");
            entity.setField("city", "City" + std::to_string(randomInt(1, 10)));
            entity.setField("age", randomInt(18, 80));
            entity.setField("salary", randomInt(30000, 150000));
            entity.setField("nickname", i % 5 == 0 ? themis::Value{} : themis::Value{makeRandomString(8)});
            entity.setField("bio", makeRandomString(300));
            
            secondary_->put("Person", entity);

            if ((i + 1) % 10000 == 0) {
                std::cout << "  Created " << (i + 1) << " / " << NUM_ENTITIES << " entities\n";
            }
        }

        std::cout << "Setup complete.\n";
        setup_done_ = true;
    }

    void TearDown(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
            secondary_.reset();
            db_.reset();
            cleanupTestDB(db_path_);
        }
    }

protected:
    std::string db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::SecondaryIndexManager> secondary_;
    static inline bool setup_done_ = false;
};

// --- Rebuild Benchmarks ---

BENCHMARK_DEFINE_F(RebuildFixture, Rebuild_Regular_Email)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->rebuildIndex("Person", "email");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES);
}
BENCHMARK_REGISTER_F(RebuildFixture, Rebuild_Regular_Email)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK_DEFINE_F(RebuildFixture, Rebuild_Composite_CityAge)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->rebuildIndex("Person", "city+age");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES);
}
BENCHMARK_REGISTER_F(RebuildFixture, Rebuild_Composite_CityAge)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK_DEFINE_F(RebuildFixture, Rebuild_Range_Salary)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->rebuildIndex("Person", "salary");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES);
}
BENCHMARK_REGISTER_F(RebuildFixture, Rebuild_Range_Salary)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK_DEFINE_F(RebuildFixture, Rebuild_Sparse_Nickname)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->rebuildIndex("Person", "nickname");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES);
}
BENCHMARK_REGISTER_F(RebuildFixture, Rebuild_Sparse_Nickname)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK_DEFINE_F(RebuildFixture, Rebuild_TTL_ExpiresAt)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->rebuildIndex("Person", "expires_at");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES);
}
BENCHMARK_REGISTER_F(RebuildFixture, Rebuild_TTL_ExpiresAt)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK_DEFINE_F(RebuildFixture, Rebuild_Fulltext_Bio)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->rebuildIndex("Person", "bio");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES);
}
BENCHMARK_REGISTER_F(RebuildFixture, Rebuild_Fulltext_Bio)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(3);

BENCHMARK_DEFINE_F(RebuildFixture, ReindexEntireTable)(benchmark::State& state) {
    for (auto _ : state) {
        secondary_->reindexTable("Person");
    }
    state.SetItemsProcessed(state.iterations() * NUM_ENTITIES * 7);
}
BENCHMARK_REGISTER_F(RebuildFixture, ReindexEntireTable)
    ->Unit(benchmark::kMillisecond)
    ->Iterations(1);

BENCHMARK_MAIN();
