#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <chrono>

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "query/query_engine.h"

using namespace themis;

static std::string tmpPath2(const std::string& name) {
    namespace fs = std::filesystem;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / (name + std::to_string(now))).string();
}

TEST(QueryEngineRangeTest, RangeWithOrderByAscendingLimit) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath2("vccdb_qe_range_"); cfg.enable_blobdb=false;
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    ASSERT_TRUE(idx.createRangeIndex("users","age").ok);

    auto put = [&](const std::string& pk, const std::string& age){
        BaseEntity::FieldMap f{{"age", age}}; // String-encoding für lexicographische Ordnung
        auto e = BaseEntity::fromFields(pk, f);
        ASSERT_TRUE(idx.put("users", e).ok);
    };
    put("u20","20"); put("u25","25"); put("u30","30"); put("u35","35");

    QueryEngine engine(db, idx);
    ConjunctiveQuery q; q.table = "users";
    q.rangePredicates.push_back({"age", std::make_optional<std::string>("20"), std::make_optional<std::string>("35"), true, true});
    q.orderBy = OrderBy{"age", false, 3};

    auto [st, keys] = engine.executeAndKeys(q);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(keys.size(), 3u);
    EXPECT_EQ(keys[0], "u20");
    EXPECT_EQ(keys[1], "u25");
    EXPECT_EQ(keys[2], "u30");
    db.close();
}

TEST(QueryEngineRangeTest, RangeExclusive) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath2("vccdb_qe_range_ex_");
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    ASSERT_TRUE(idx.createRangeIndex("users","age").ok);

    auto put = [&](const std::string& pk, const std::string& age){
        auto e = BaseEntity::fromFields(pk, {{"age", age}});
        ASSERT_TRUE(idx.put("users", e).ok);
    };
    put("u20","20"); put("u25","25"); put("u30","30"); put("u35","35");

    QueryEngine engine(db, idx);
    ConjunctiveQuery q{"users"};
    q.rangePredicates.push_back({"age", std::make_optional<std::string>("20"), std::make_optional<std::string>("35"), false, false});

    auto [st, keys] = engine.executeAndKeys(q);
    ASSERT_TRUE(st.ok);
    std::sort(keys.begin(), keys.end());
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], "u25");
    EXPECT_EQ(keys[1], "u30");
    db.close();
}

TEST(QueryEngineRangeTest, OrderByDescending) {
    RocksDBWrapper::Config cfg; cfg.db_path = tmpPath2("vccdb_qe_range_desc_");
    RocksDBWrapper db(cfg); ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);
    ASSERT_TRUE(idx.createRangeIndex("users","age").ok);

    for (auto v : {"20","25","30","35"}) {
        ASSERT_TRUE(idx.put("users", BaseEntity::fromFields(std::string("u")+v, {{"age", std::string(v)}})).ok);
    }
    QueryEngine engine(db, idx);
    ConjunctiveQuery q; q.table = "users";
    q.orderBy = OrderBy{"age", true, 2};

    auto [st, keys] = engine.executeAndKeys(q);
    ASSERT_TRUE(st.ok);
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0], "u35");
    EXPECT_EQ(keys[1], "u30");
    db.close();
}
