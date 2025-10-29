// MVCC Performance Benchmarks
// Vergleicht MVCC TransactionWrapper vs. WriteBatch Performance

#include <benchmark/benchmark.h>
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include <filesystem>
#include <random>

using namespace themis;

class MVCCFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State& state) override {
        std::filesystem::remove_all("data/bench_mvcc");
        
        RocksDBWrapper::Config config;
        config.db_path = "data/bench_mvcc";
        config.memtable_size_mb = 128;
        config.block_cache_size_mb = 256;
        
        db_ = std::make_unique<RocksDBWrapper>(config);
        db_->open();
        
        secIdx_ = std::make_unique<SecondaryIndexManager>(*db_);
        secIdx_->createIndex("users", "email");
        secIdx_->createIndex("users", "age");
    }

    void TearDown(const ::benchmark::State& state) override {
        secIdx_.reset();
        db_->close();
        db_.reset();
        std::filesystem::remove_all("data/bench_mvcc");
    }

    BaseEntity createTestEntity(const std::string& pk, int age) {
        BaseEntity e(pk);
        e.setField("email", pk + "@test.com");
        e.setField("age", std::to_string(age));
        e.setField("name", "User" + pk);
        return e;
    }

    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> secIdx_;
};

// ============================================================================
// MVCC Transaction Benchmarks
// ============================================================================

BENCHMARK_F(MVCCFixture, SingleEntityCommit_MVCC)(benchmark::State& state) {
    int counter = 0;
    for (auto _ : state) {
        auto txn = db_->beginTransaction();
        auto entity = createTestEntity("user" + std::to_string(counter++), 25);
        
        std::string key = "entity:users:" + entity.getPrimaryKey();
        txn->put(key, entity.serialize());
        secIdx_->put("users", entity, *txn);
        
        txn->commit();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MVCCFixture, BatchInsert100_MVCC)(benchmark::State& state) {
    int counter = 0;
    for (auto _ : state) {
        auto txn = db_->beginTransaction();
        for (int i = 0; i < 100; ++i) {
            auto entity = createTestEntity("user" + std::to_string(counter++), 20 + (i % 50));
            std::string key = "entity:users:" + entity.getPrimaryKey();
            txn->put(key, entity.serialize());
            secIdx_->put("users", entity, *txn);
        }
        txn->commit();
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

BENCHMARK_F(MVCCFixture, SnapshotIsolationOverhead_MVCC)(benchmark::State& state) {
    // Pre-populate
    auto txn = db_->beginTransaction();
    for (int i = 0; i < 100; ++i) {
        auto entity = createTestEntity("user" + std::to_string(i), 25);
        std::string key = "entity:users:" + entity.getPrimaryKey();
        txn->put(key, entity.serialize());
        secIdx_->put("users", entity, *txn);
    }
    txn->commit();
    
    for (auto _ : state) {
        auto txn = db_->beginTransaction();
        
        // Multiple reads within same snapshot
        for (int i = 0; i < 10; ++i) {
            auto result = txn->get("entity:users:user" + std::to_string(i));
        }
        
        txn->commit();
    }
    state.SetItemsProcessed(state.iterations() * 10);
}

BENCHMARK_F(MVCCFixture, Rollback_MVCC)(benchmark::State& state) {
    int counter = 0;
    for (auto _ : state) {
        auto txn = db_->beginTransaction();
        for (int i = 0; i < 10; ++i) {
            auto entity = createTestEntity("user" + std::to_string(counter++), 25);
            std::string key = "entity:users:" + entity.getPrimaryKey();
            txn->put(key, entity.serialize());
            secIdx_->put("users", entity, *txn);
        }
        txn->rollback();
    }
    state.SetItemsProcessed(state.iterations() * 10);
}

// ============================================================================
// WriteBatch Comparison Benchmarks
// ============================================================================

BENCHMARK_F(MVCCFixture, SingleEntityCommit_WriteBatch)(benchmark::State& state) {
    int counter = 0;
    for (auto _ : state) {
        auto batch = db_->createWriteBatch();
        auto entity = createTestEntity("batch_user" + std::to_string(counter++), 25);
        
        std::string key = "entity:users:" + entity.getPrimaryKey();
        batch->put(key, entity.serialize());
        secIdx_->put("users", entity, *batch);
        
        batch->commit();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MVCCFixture, BatchInsert100_WriteBatch)(benchmark::State& state) {
    int counter = 0;
    for (auto _ : state) {
        auto batch = db_->createWriteBatch();
        for (int i = 0; i < 100; ++i) {
            auto entity = createTestEntity("batch_user" + std::to_string(counter++), 20 + (i % 50));
            std::string key = "entity:users:" + entity.getPrimaryKey();
            batch->put(key, entity.serialize());
            secIdx_->put("users", entity, *batch);
        }
        batch->commit();
    }
    state.SetItemsProcessed(state.iterations() * 100);
}

// ============================================================================
// Index-Heavy Workloads
// ============================================================================

BENCHMARK_F(MVCCFixture, InsertWithMultipleIndexes_MVCC)(benchmark::State& state) {
    // Create multiple indexes
    secIdx_->createIndex("users", "name");
    
    int counter = 0;
    for (auto _ : state) {
        auto txn = db_->beginTransaction();
        auto entity = createTestEntity("user" + std::to_string(counter++), 25);
        std::string key = "entity:users:" + entity.getPrimaryKey();
        txn->put(key, entity.serialize());
        secIdx_->put("users", entity, *txn);
        txn->commit();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_F(MVCCFixture, UpdateWithIndexes_MVCC)(benchmark::State& state) {
    // Pre-populate
    auto txn = db_->beginTransaction();
    for (int i = 0; i < 1000; ++i) {
        auto entity = createTestEntity("user" + std::to_string(i), 20);
        std::string key = "entity:users:" + entity.getPrimaryKey();
        txn->put(key, entity.serialize());
        secIdx_->put("users", entity, *txn);
    }
    txn->commit();
    
    int counter = 0;
    for (auto _ : state) {
        auto txn = db_->beginTransaction();
        int id = counter++ % 1000;
        auto entity = createTestEntity("user" + std::to_string(id), 30); // Age change triggers index update
        std::string key = "entity:users:" + entity.getPrimaryKey();
        txn->put(key, entity.serialize());
        secIdx_->put("users", entity, *txn);
        txn->commit();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK_MAIN();
