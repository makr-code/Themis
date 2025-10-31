// Tests for Semantic Query Cache

#include <gtest/gtest.h>
#include "query/semantic_cache.h"
#include "storage/rocksdb_wrapper.h"
#include "index/vector_index.h"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace themis;

class SemanticCacheTest : public ::testing::Test {
protected:
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<VectorIndexManager> vim;
    std::unique_ptr<SemanticQueryCache> cache;
    std::string testDbPath = "data/themis_semantic_cache_test";
    
    void SetUp() override {
        // Clean up existing test database
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove_all(testDbPath);
        }
        
        // Initialize components
        RocksDBWrapper::Config config;
        config.db_path = testDbPath;
        db = std::make_unique<RocksDBWrapper>(config);
        db->open();
        
        vim = std::make_unique<VectorIndexManager>(*db);
        
        // Create cache with test config
        SemanticQueryCache::Config cacheConfig;
        cacheConfig.max_entries = 10;
        cacheConfig.similarity_threshold = 0.85f;
        cacheConfig.ttl = std::chrono::seconds(10);  // Short TTL for testing
        
        cache = std::make_unique<SemanticQueryCache>(*db, *vim, cacheConfig);
    }
    
    void TearDown() override {
        cache.reset();
        vim.reset();
        db.reset();
        
        if (std::filesystem::exists(testDbPath)) {
            std::filesystem::remove_all(testDbPath);
        }
    }
};

TEST_F(SemanticCacheTest, PutAndGetExactMatch) {
    std::string query = "FIND users WHERE age > 30";
    std::string result = R"({"users": [{"id": "1", "name": "Alice"}]})";
    
    // Put into cache
    auto st = cache->put(query, result);
    EXPECT_TRUE(st.ok);
    
    // Get with exact match
    auto lookup = cache->get(query);
    EXPECT_TRUE(lookup.found);
    EXPECT_TRUE(lookup.exact_match);
    EXPECT_EQ(lookup.result_json, result);
    EXPECT_FLOAT_EQ(lookup.similarity, 1.0f);
    EXPECT_EQ(lookup.matched_query, query);
    
    // Check stats
    auto stats = cache->getStats();
    EXPECT_EQ(stats.total_lookups, 1);
    EXPECT_EQ(stats.exact_hits, 1);
    EXPECT_EQ(stats.similarity_hits, 0);
    EXPECT_EQ(stats.misses, 0);
}

TEST_F(SemanticCacheTest, CacheMiss) {
    std::string query = "FIND users WHERE age > 30";
    
    // Get without putting
    auto lookup = cache->get(query);
    EXPECT_FALSE(lookup.found);
    
    // Check stats
    auto stats = cache->getStats();
    EXPECT_EQ(stats.total_lookups, 1);
    EXPECT_EQ(stats.misses, 1);
}

TEST_F(SemanticCacheTest, SimilarityMatch) {
    std::string query1 = "FIND users WHERE age > 30";
    std::string query2 = "FIND users WHERE age > 35";  // Similar query
    std::string result1 = R"({"users": [{"id": "1"}]})";
    
    // Put first query
    cache->put(query1, result1);
    
    // Get similar query
    auto lookup = cache->get(query2);
    EXPECT_TRUE(lookup.found);
    EXPECT_FALSE(lookup.exact_match);  // Not exact
    EXPECT_GT(lookup.similarity, 0.85f);  // Should be similar
    EXPECT_EQ(lookup.result_json, result1);  // Same result
    
    // Check stats
    auto stats = cache->getStats();
    EXPECT_EQ(stats.similarity_hits, 1);
}

TEST_F(SemanticCacheTest, DissimilarQueryMiss) {
    std::string query1 = "FIND users WHERE age > 30";
    std::string query2 = "CREATE INDEX ON comments(timestamp)";  // Completely different (DDL vs DQL)
    std::string result1 = R"({"users": []})";
    
    cache->put(query1, result1);
    
    // Get dissimilar query
    auto lookup = cache->get(query2);
    // May or may not find (depends on similarity threshold)
    if (lookup.found) {
        EXPECT_LT(lookup.similarity, 0.85f);  // Should be below threshold
    }
}

TEST_F(SemanticCacheTest, LRUEviction) {
    // Fill cache to max (10 entries)
    for (int i = 0; i < 10; ++i) {
        std::string query = "FIND users WHERE id = " + std::to_string(i);
        std::string result = R"({"user": {"id": ")" + std::to_string(i) + R"("}})";
        cache->put(query, result);
    }
    
    auto stats = cache->getStats();
    EXPECT_EQ(stats.current_entries, 10);
    
    // Add one more - should evict LRU
    std::string newQuery = "FIND users WHERE id = 99";
    cache->put(newQuery, R"({"user": {"id": "99"}})");
    
    stats = cache->getStats();
    EXPECT_EQ(stats.current_entries, 10);  // Still max
    EXPECT_EQ(stats.evictions, 1);
}

TEST_F(SemanticCacheTest, TTLExpiration) {
    std::string query = "FIND users WHERE age > 30";
    std::string result = R"({"users": []})";
    
    cache->put(query, result);
    
    // Immediate get - should hit
    auto lookup1 = cache->get(query);
    EXPECT_TRUE(lookup1.found);
    
    // Wait for TTL to expire (10 seconds)
    std::this_thread::sleep_for(std::chrono::seconds(11));
    
    // Get after expiration - should miss
    auto lookup2 = cache->get(query);
    EXPECT_FALSE(lookup2.found);
}

TEST_F(SemanticCacheTest, ManualEviction) {
    for (int i = 0; i < 5; ++i) {
        cache->put("FIND users " + std::to_string(i), R"({"result": []})");
    }
    
    auto stats1 = cache->getStats();
    EXPECT_EQ(stats1.current_entries, 5);
    
    // Evict 2 entries
    auto st = cache->evictLRU(2);
    EXPECT_TRUE(st.ok);
    
    auto stats2 = cache->getStats();
    EXPECT_EQ(stats2.current_entries, 3);
    EXPECT_EQ(stats2.evictions, 2);
}

TEST_F(SemanticCacheTest, RemoveEntry) {
    std::string query = "FIND users WHERE age > 30";
    cache->put(query, R"({"users": []})");
    
    // Verify exists
    auto lookup1 = cache->get(query);
    EXPECT_TRUE(lookup1.found);
    
    // Remove
    auto st = cache->remove(query);
    EXPECT_TRUE(st.ok);
    
    // Verify removed
    auto lookup2 = cache->get(query);
    EXPECT_FALSE(lookup2.found);
    
    auto stats = cache->getStats();
    EXPECT_EQ(stats.current_entries, 0);
}

TEST_F(SemanticCacheTest, ClearCache) {
    for (int i = 0; i < 5; ++i) {
        cache->put("FIND users " + std::to_string(i), R"({})");
    }
    
    auto stats1 = cache->getStats();
    EXPECT_EQ(stats1.current_entries, 5);
    
    // Clear
    auto st = cache->clear();
    EXPECT_TRUE(st.ok);
    
    auto stats2 = cache->getStats();
    EXPECT_EQ(stats2.current_entries, 0);
    EXPECT_EQ(stats2.total_result_bytes, 0);
}

TEST_F(SemanticCacheTest, HitRateCalculation) {
    std::string query = "FIND users";
    cache->put(query, R"({})");
    
    // 3 hits (1 exact, 2 similarity)
    cache->get(query);                      // exact hit
    cache->get("FIND users WHERE true");    // similarity hit (maybe)
    cache->get("FIND users LIMIT 10");      // similarity hit (maybe)
    
    // 2 misses
    cache->get("FIND products");
    cache->get("DELETE users");
    
    auto stats = cache->getStats();
    EXPECT_EQ(stats.total_lookups, 5);
    EXPECT_GT(stats.hitRate(), 0.0f);
    EXPECT_LE(stats.hitRate(), 1.0f);
}

TEST_F(SemanticCacheTest, ConfigUpdate) {
    auto config1 = cache->getConfig();
    EXPECT_EQ(config1.max_entries, 10);
    
    SemanticQueryCache::Config newConfig;
    newConfig.max_entries = 20;
    newConfig.similarity_threshold = 0.9f;
    
    cache->setConfig(newConfig);
    
    auto config2 = cache->getConfig();
    EXPECT_EQ(config2.max_entries, 20);
    EXPECT_FLOAT_EQ(config2.similarity_threshold, 0.9f);
}

TEST_F(SemanticCacheTest, EmptyInputRejection) {
    auto st1 = cache->put("", R"({})");
    EXPECT_FALSE(st1.ok);
    
    auto st2 = cache->put("FIND users", "");
    EXPECT_FALSE(st2.ok);
}

TEST_F(SemanticCacheTest, HitCountTracking) {
    std::string query = "FIND users";
    cache->put(query, R"({})");
    
    // Hit multiple times
    for (int i = 0; i < 3; ++i) {
        auto lookup = cache->get(query);
        EXPECT_TRUE(lookup.found);
    }
    
    // Check hit count (would need accessor to verify internal state)
    // For now, just verify it doesn't crash
}

TEST_F(SemanticCacheTest, ConcurrentAccess) {
    std::string query = "FIND users";
    cache->put(query, R"({})");
    
    // Concurrent reads
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([this, &query]() {
            for (int j = 0; j < 10; ++j) {
                auto lookup = cache->get(query);
                EXPECT_TRUE(lookup.found);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    auto stats = cache->getStats();
    EXPECT_EQ(stats.total_lookups, 50);
}
