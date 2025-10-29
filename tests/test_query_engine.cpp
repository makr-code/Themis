// QueryEngine index-backed equality tests

#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "query/query_engine.h"
#include "query/query_optimizer.h"

using namespace themis;

static std::string tmpPath(const std::string& name) {
    namespace fs = std::filesystem;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / (name + std::to_string(now))).string();
}

TEST(QueryEngineTest, AndQuery_UsesSecondaryIndexes) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath("vccdb_qe_"); cfg.enable_blobdb=false;
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    ASSERT_TRUE(idx.createIndex("users","age").ok);
    ASSERT_TRUE(idx.createIndex("users","city").ok);

    // Data
    auto put = [&](const std::string& pk, int age, const std::string& city){
        BaseEntity::FieldMap f{{"name","N"+pk},{"age", int64_t(age)},{"city", city}};
        BaseEntity e = BaseEntity::fromFields(pk, f);
        ASSERT_TRUE(idx.put("users", e).ok);
    };
    put("u1", 30, "Berlin");
    put("u2", 31, "Berlin");
    put("u3", 30, "Munich");

    QueryEngine engine(db, idx);
    ConjunctiveQuery q{"users", {{"age","30"},{"city","Berlin"}}};

    // Parallel execution
    auto [stK, keys] = engine.executeAndKeys(q);
    ASSERT_TRUE(stK.ok) << stK.message;
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], "u1");

    auto [stE, ents] = engine.executeAndEntities(q);
    ASSERT_TRUE(stE.ok);
    ASSERT_EQ(ents.size(), 1u);
    db.close();
}

TEST(QueryEngineTest, OptimizedSequentialOrder) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath("vccdb_qe_opt_");
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    ASSERT_TRUE(idx.createIndex("users","age").ok);
    ASSERT_TRUE(idx.createIndex("users","city").ok);

    for (int i=0;i<50;++i) {
        BaseEntity::FieldMap f{{"name","N"+std::to_string(i)},{"age", int64_t(30)},{"city","Berlin"}};
        BaseEntity e = BaseEntity::fromFields("u"+std::to_string(i), f);
        ASSERT_TRUE(idx.put("users", e).ok);
    }
    // make a rare predicate
    BaseEntity::FieldMap f{{"name","Rare"},{"age", int64_t(99)},{"city","Berlin"}};
    ASSERT_TRUE(idx.put("users", BaseEntity::fromFields("rare", f)).ok);

    QueryEngine engine(db, idx);
    QueryOptimizer opt(idx);
    ConjunctiveQuery q{"users", {{"age","99"},{"city","Berlin"}}};

    auto plan = opt.chooseOrderForAndQuery(q, 10);
    ASSERT_EQ(plan.orderedPredicates.size(), 2u);
    // age=99 should be first due to low estimate
    EXPECT_EQ(plan.orderedPredicates[0].column, "age");

    auto [stK, keys] = opt.executeOptimizedKeys(engine, q, plan);
    ASSERT_TRUE(stK.ok);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], "rare");
    db.close();
}

TEST(QueryEngineTest, NoIndexReturnsError) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath("vccdb_qe_noidx_");
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    // Kein Index angelegt
    // Daten schreiben direkt via Indexmanager (pflegt hier keine Indizes da keine Meta-Keys existieren, aber entspr. Relationseintrag wird geschrieben)
    BaseEntity e = BaseEntity::fromFields("u1", {{"age", int64_t(30)}});
    // Um Relationseintrag zu erstellen, nutzen wir put auf Tabelle users (ohne Index keine Index-Keys)
    ASSERT_TRUE(idx.put("users", e).ok);

    QueryEngine engine(db, idx);
    ConjunctiveQuery q{"users", {{"age","30"}}};
    auto [stK, keys] = engine.executeAndKeys(q);
    EXPECT_FALSE(stK.ok);
    db.close();
}

TEST(QueryEngineTest, NoMatchReturnsEmpty) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath("vccdb_qe_nomatch_");
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    ASSERT_TRUE(idx.createIndex("users","age").ok);

    BaseEntity e = BaseEntity::fromFields("u1", {{"age", int64_t(30)}});
    ASSERT_TRUE(idx.put("users", e).ok);

    QueryEngine engine(db, idx);
    ConjunctiveQuery q{"users", {{"age","99"}}};
    auto [stK, keys] = engine.executeAndKeys(q);
    ASSERT_TRUE(stK.ok);
    EXPECT_TRUE(keys.empty());
    db.close();
}

TEST(QueryEngineTest, NoIndexWithFallbackReturnsKeys) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath("vccdb_qe_fallback_");
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    // Kein Index
    BaseEntity e = BaseEntity::fromFields("u1", {{"age", int64_t(30)}, {"city","Berlin"}});
    ASSERT_TRUE(idx.put("users", e).ok);

    QueryEngine engine(db, idx);
    ConjunctiveQuery q{"users", {{"age","30"}, {"city","Berlin"}}};
    auto [stK, keys] = engine.executeAndKeysWithFallback(q, true);
    ASSERT_TRUE(stK.ok);
    ASSERT_EQ(keys.size(), 1u);
    EXPECT_EQ(keys[0], "u1");
    db.close();
}
