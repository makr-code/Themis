#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

#include "storage/key_schema.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"

using namespace themis;

// ----------------- KeySchema unit tests (existing) -----------------

TEST(KeySchemaTest, MakeRelationalKey) {
    std::string key = KeySchema::makeRelationalKey("users", "123");
    EXPECT_EQ(key, "users:123");
}

TEST(KeySchemaTest, MakeGraphNodeKey) {
    std::string key = KeySchema::makeGraphNodeKey("user/alice");
    EXPECT_EQ(key, "node:user/alice");
}

TEST(KeySchemaTest, MakeGraphEdgeKey) {
    std::string key = KeySchema::makeGraphEdgeKey("edge_1");
    EXPECT_EQ(key, "edge:edge_1");
}

TEST(KeySchemaTest, MakeSecondaryIndexKey) {
    std::string key = KeySchema::makeSecondaryIndexKey("users", "age", "30", "user_123");
    EXPECT_EQ(key, "idx:users:age:30:user_123");
}

TEST(KeySchemaTest, MakeGraphOutdexKey) {
    std::string key = KeySchema::makeGraphOutdexKey("user/alice", "edge_1");
    EXPECT_EQ(key, "graph:out:user/alice:edge_1");
}

TEST(KeySchemaTest, MakeGraphIndexKey) {
    std::string key = KeySchema::makeGraphIndexKey("company/acme", "edge_1");
    EXPECT_EQ(key, "graph:in:company/acme:edge_1");
}

TEST(KeySchemaTest, ExtractPrimaryKey) {
    std::string pk = KeySchema::extractPrimaryKey("users:123");
    EXPECT_EQ(pk, "123");
    
    pk = KeySchema::extractPrimaryKey("idx:users:age:30:user_456");
    EXPECT_EQ(pk, "user_456");
}

TEST(KeySchemaTest, ParseKeyType) {
    EXPECT_EQ(KeySchema::parseKeyType("idx:users:age:30:pk"), KeySchema::KeyType::SECONDARY_INDEX);
    EXPECT_EQ(KeySchema::parseKeyType("graph:out:alice:e1"), KeySchema::KeyType::GRAPH_OUTDEX);
    EXPECT_EQ(KeySchema::parseKeyType("graph:in:bob:e1"), KeySchema::KeyType::GRAPH_INDEX);
    EXPECT_EQ(KeySchema::parseKeyType("node:alice"), KeySchema::KeyType::GRAPH_NODE);
    EXPECT_EQ(KeySchema::parseKeyType("edge:e1"), KeySchema::KeyType::GRAPH_EDGE);
}

// ----------------- SecondaryIndex integration tests -----------------

static std::string makeTempDbPath(const std::string& name) {
    namespace fs = std::filesystem;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    auto base = fs::temp_directory_path() / (name + std::to_string(now));
    return base.string();
}

TEST(SecondaryIndexTest, CreatePutScanDelete) {
    // Arrange: RocksDB wrapper
    RocksDBWrapper::Config cfg;
    cfg.db_path = makeTempDbPath("vccdb_secidx_create_put_");
    cfg.enable_blobdb = false; // not needed in tests
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());

    SecondaryIndexManager idx(db);
    auto st = idx.createIndex("users", "age");
    ASSERT_TRUE(st.ok) << st.message;

    // Insert entity
    BaseEntity::FieldMap fields1{{"name","Alice"}, {"age", int64_t(30)}, {"city","Berlin"}};
    BaseEntity e1 = BaseEntity::fromFields("u1", fields1);
    st = idx.put("users", e1);
    ASSERT_TRUE(st.ok) << st.message;

    // Scan equals age=30 -> expect u1
    auto [st1, keys] = idx.scanKeysEqual("users", "age", "30");
    ASSERT_TRUE(st1.ok) << st1.message;
    ASSERT_EQ(keys.size(), 1);
    EXPECT_EQ(keys[0], "u1");

    // Update: change age to 31
    BaseEntity::FieldMap fields2{{"name","Alice"}, {"age", int64_t(31)}, {"city","Berlin"}};
    BaseEntity e2 = BaseEntity::fromFields("u1", fields2);
    st = idx.put("users", e2);
    ASSERT_TRUE(st.ok) << st.message;

    // Old index should be gone
    auto [st2a, keys_old] = idx.scanKeysEqual("users", "age", "30");
    ASSERT_TRUE(st2a.ok);
    EXPECT_TRUE(keys_old.empty());
    // New index
    auto [st2b, keys_new] = idx.scanKeysEqual("users", "age", "31");
    ASSERT_TRUE(st2b.ok);
    ASSERT_EQ(keys_new.size(), 1);
    EXPECT_EQ(keys_new[0], "u1");

    // Delete entity
    st = idx.erase("users", "u1");
    ASSERT_TRUE(st.ok) << st.message;
    auto [st3, keys_post] = idx.scanKeysEqual("users", "age", "31");
    ASSERT_TRUE(st3.ok);
    EXPECT_TRUE(keys_post.empty());

    db.close();
}

TEST(SecondaryIndexTest, EstimateCountAndNoIndex) {
    RocksDBWrapper::Config cfg;
    cfg.db_path = makeTempDbPath("vccdb_secidx_estimate_");
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);

    // No index yet -> scans should error, estimate = 0
    auto [st0, keys0] = idx.scanKeysEqual("users", "age", "30");
    EXPECT_FALSE(st0.ok);
    bool capped=false; 
    EXPECT_EQ(idx.estimateCountEqual("users","age","30", 10, &capped), 0u);
    EXPECT_FALSE(capped);

    // Create index and insert 3 entries with same age
    ASSERT_TRUE(idx.createIndex("users","age").ok);
    for (int i=0; i<3; ++i) {
        BaseEntity::FieldMap f{{"name","N"+std::to_string(i)}, {"age", int64_t(30)}};
        BaseEntity e = BaseEntity::fromFields("u"+std::to_string(i), f);
        ASSERT_TRUE(idx.put("users", e).ok);
    }

    capped = false;
    auto c = idx.estimateCountEqual("users","age","30", 2, &capped);
    EXPECT_EQ(c, 2u);
    EXPECT_TRUE(capped);

    auto [st1, keys] = idx.scanKeysEqual("users","age","30");
    ASSERT_TRUE(st1.ok);
    EXPECT_EQ(keys.size(), 3u);

    db.close();
}
