#include <gtest/gtest.h>
#include "query/query_engine.h"
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>

using namespace themis;

class QueryOrTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/query_or_test";
        
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = db_path;
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 128;
        db_ = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db_->open());

        secIdx_ = std::make_unique<SecondaryIndexManager>(*db_);
        engine_ = std::make_unique<QueryEngine>(*db_, *secIdx_);

        setupTestData();
    }

    void TearDown() override {
        engine_.reset();
        secIdx_.reset();
        db_->close();
        db_.reset();
        
        std::filesystem::remove_all("data/query_or_test");
    }

    void setupTestData() {
        // Create index on city and age
        auto st1 = secIdx_->createIndex("users", "city");
        ASSERT_TRUE(st1.ok) << st1.message;
        
        auto st2 = secIdx_->createIndex("users", "age");
        ASSERT_TRUE(st2.ok) << st2.message;

        // Insert test users
        BaseEntity alice("alice");
        alice.setField("city", "Berlin");
        alice.setField("age", 25);
        db_->put("users:alice", alice.serialize());
        auto st_alice = secIdx_->put("users", alice);
        ASSERT_TRUE(st_alice.ok) << st_alice.message;

        BaseEntity bob("bob");
        bob.setField("city", "Munich");
        bob.setField("age", 30);
        db_->put("users:bob", bob.serialize());
        auto st_bob = secIdx_->put("users", bob);
        ASSERT_TRUE(st_bob.ok) << st_bob.message;

        BaseEntity charlie("charlie");
        charlie.setField("city", "Berlin");
        charlie.setField("age", 35);
        db_->put("users:charlie", charlie.serialize());
        auto st_charlie = secIdx_->put("users", charlie);
        ASSERT_TRUE(st_charlie.ok) << st_charlie.message;

        BaseEntity diana("diana");
        diana.setField("city", "Hamburg");
        diana.setField("age", 28);
        db_->put("users:diana", diana.serialize());
        auto st_diana = secIdx_->put("users", diana);
        ASSERT_TRUE(st_diana.ok) << st_diana.message;
    }

    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> secIdx_;
    std::unique_ptr<QueryEngine> engine_;
};

TEST_F(QueryOrTest, OrQuery_CityBerlinOrMunich) {
    // Query: city==Berlin OR city==Munich
    DisjunctiveQuery q;
    q.table = "users";
    
    ConjunctiveQuery berlin_query;
    berlin_query.table = "users";
    berlin_query.predicates.push_back(PredicateEq{"city", "Berlin"});
    
    ConjunctiveQuery munich_query;
    munich_query.table = "users";
    munich_query.predicates.push_back(PredicateEq{"city", "Munich"});
    
    q.disjuncts.push_back(berlin_query);
    q.disjuncts.push_back(munich_query);
    
    auto [st, keys] = engine_->executeOrKeys(q);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should return alice, charlie (Berlin) + bob (Munich) = 3 results
    ASSERT_EQ(keys.size(), 3);
    
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "alice");
    EXPECT_EQ(keys[1], "bob");
    EXPECT_EQ(keys[2], "charlie");
}

TEST_F(QueryOrTest, OrQuery_Age25Or30) {
    // Query: age==25 OR age==30
    DisjunctiveQuery q;
    q.table = "users";
    
    ConjunctiveQuery age25_query;
    age25_query.table = "users";
    age25_query.predicates.push_back(PredicateEq{"age", "25"});
    
    ConjunctiveQuery age30_query;
    age30_query.table = "users";
    age30_query.predicates.push_back(PredicateEq{"age", "30"});
    
    q.disjuncts.push_back(age25_query);
    q.disjuncts.push_back(age30_query);
    
    auto [st, keys] = engine_->executeOrKeys(q);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should return alice (25) + bob (30) = 2 results
    ASSERT_EQ(keys.size(), 2);
    
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "alice");
    EXPECT_EQ(keys[1], "bob");
}

TEST_F(QueryOrTest, OrQuery_ComplexConditions) {
    // Query: (city==Berlin AND age==25) OR (city==Munich AND age==30)
    DisjunctiveQuery q;
    q.table = "users";
    
    ConjunctiveQuery berlin_25;
    berlin_25.table = "users";
    berlin_25.predicates.push_back(PredicateEq{"city", "Berlin"});
    berlin_25.predicates.push_back(PredicateEq{"age", "25"});
    
    ConjunctiveQuery munich_30;
    munich_30.table = "users";
    munich_30.predicates.push_back(PredicateEq{"city", "Munich"});
    munich_30.predicates.push_back(PredicateEq{"age", "30"});
    
    q.disjuncts.push_back(berlin_25);
    q.disjuncts.push_back(munich_30);
    
    auto [st, keys] = engine_->executeOrKeys(q);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should return alice (Berlin, 25) + bob (Munich, 30) = 2 results
    ASSERT_EQ(keys.size(), 2);
    
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "alice");
    EXPECT_EQ(keys[1], "bob");
}

TEST_F(QueryOrTest, OrQuery_NoDuplicates) {
    // Query: city==Berlin OR age==25
    // alice matches both conditions, should appear only once
    DisjunctiveQuery q;
    q.table = "users";
    
    ConjunctiveQuery berlin_query;
    berlin_query.table = "users";
    berlin_query.predicates.push_back(PredicateEq{"city", "Berlin"});
    
    ConjunctiveQuery age25_query;
    age25_query.table = "users";
    age25_query.predicates.push_back(PredicateEq{"age", "25"});
    
    q.disjuncts.push_back(berlin_query);
    q.disjuncts.push_back(age25_query);
    
    auto [st, keys] = engine_->executeOrKeys(q);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should return alice, charlie (Berlin) + alice (age 25) but deduplicated = 2 unique
    ASSERT_EQ(keys.size(), 2);
    
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "alice");
    EXPECT_EQ(keys[1], "charlie");
}

TEST_F(QueryOrTest, OrQuery_EmptyDisjunct) {
    // Query with one non-matching disjunct should still return results from other disjuncts
    DisjunctiveQuery q;
    q.table = "users";
    
    ConjunctiveQuery nonexistent_query;
    nonexistent_query.table = "users";
    nonexistent_query.predicates.push_back(PredicateEq{"city", "Tokyo"}); // No match
    
    ConjunctiveQuery berlin_query;
    berlin_query.table = "users";
    berlin_query.predicates.push_back(PredicateEq{"city", "Berlin"});
    
    q.disjuncts.push_back(nonexistent_query);
    q.disjuncts.push_back(berlin_query);
    
    auto [st, keys] = engine_->executeOrKeys(q);
    ASSERT_TRUE(st.ok) << st.message;
    
    // Should return alice, charlie (Berlin only)
    ASSERT_EQ(keys.size(), 2);
    
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "alice");
    EXPECT_EQ(keys[1], "charlie");
}
