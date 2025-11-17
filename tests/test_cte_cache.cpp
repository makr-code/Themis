#include <gtest/gtest.h>
#include "query/cte_cache.h"
#include <nlohmann/json.hpp>
#include <filesystem>

using namespace themis::query;
using json = nlohmann::json;

class CTECacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clean up any previous test artifacts
        if (std::filesystem::exists("./test_cte_spill")) {
            std::filesystem::remove_all("./test_cte_spill");
        }
    }
    
    void TearDown() override {
        // Clean up test artifacts
        if (std::filesystem::exists("./test_cte_spill")) {
            std::filesystem::remove_all("./test_cte_spill");
        }
    }
    
    // Helper to create test data
    std::vector<json> createTestData(size_t count) {
        std::vector<json> data;
        for (size_t i = 0; i < count; ++i) {
            data.push_back({
                {"id", i},
                {"name", "user_" + std::to_string(i)},
                {"value", i * 100}
            });
        }
        return data;
    }
};

// ============================================================================
// Phase 4.3: Basic CTECache Tests
// ============================================================================

TEST_F(CTECacheTest, BasicStoreAndGet) {
    CTECache::Config config;
    config.max_memory_bytes = 10 * 1024 * 1024; // 10MB
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    auto data = createTestData(100);
    ASSERT_TRUE(cache.store("test_cte", data));
    
    auto retrieved = cache.get("test_cte");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), 100);
    EXPECT_EQ((*retrieved)[0]["id"], 0);
    EXPECT_EQ((*retrieved)[99]["id"], 99);
}

TEST_F(CTECacheTest, MultipleCTEs) {
    CTECache::Config config;
    config.max_memory_bytes = 10 * 1024 * 1024;
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    ASSERT_TRUE(cache.store("cte1", createTestData(50)));
    ASSERT_TRUE(cache.store("cte2", createTestData(75)));
    ASSERT_TRUE(cache.store("cte3", createTestData(100)));
    
    EXPECT_TRUE(cache.contains("cte1"));
    EXPECT_TRUE(cache.contains("cte2"));
    EXPECT_TRUE(cache.contains("cte3"));
    
    auto stats = cache.getStats();
    EXPECT_EQ(stats.total_ctes, 3);
}

TEST_F(CTECacheTest, RemoveCTE) {
    CTECache::Config config;
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    auto data = createTestData(50);
    ASSERT_TRUE(cache.store("temp_cte", data));
    EXPECT_TRUE(cache.contains("temp_cte"));
    
    ASSERT_TRUE(cache.remove("temp_cte"));
    EXPECT_FALSE(cache.contains("temp_cte"));
    
    auto retrieved = cache.get("temp_cte");
    EXPECT_FALSE(retrieved.has_value());
}

// ============================================================================
// Phase 4.3: Spill-to-Disk Tests
// ============================================================================

TEST_F(CTECacheTest, AutomaticSpillToDisk) {
    CTECache::Config config;
    config.max_memory_bytes = 50 * 1024; // Very small: 50KB to force spill
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    // Store a large CTE that exceeds memory limit
    auto large_data = createTestData(1000); // Should be >50KB
    ASSERT_TRUE(cache.store("large_cte", large_data));
    
    auto stats = cache.getStats();
    EXPECT_GT(stats.spill_operations, 0); // Should have spilled
    
    // Verify we can still retrieve it
    auto retrieved = cache.get("large_cte");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), 1000);
    EXPECT_EQ((*retrieved)[0]["id"], 0);
    EXPECT_EQ((*retrieved)[999]["id"], 999);
    
    EXPECT_GT(stats.disk_reads, 0); // Should have read from disk
}

TEST_F(CTECacheTest, MultipleSpills) {
    CTECache::Config config;
    config.max_memory_bytes = 100 * 1024; // 100KB
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    // Store multiple large CTEs to trigger multiple spills
    ASSERT_TRUE(cache.store("cte1", createTestData(500)));
    ASSERT_TRUE(cache.store("cte2", createTestData(500)));
    ASSERT_TRUE(cache.store("cte3", createTestData(500)));
    
    auto stats = cache.getStats();
    EXPECT_GT(stats.spill_operations, 0);
    
    // All CTEs should still be retrievable
    EXPECT_TRUE(cache.contains("cte1"));
    EXPECT_TRUE(cache.contains("cte2"));
    EXPECT_TRUE(cache.contains("cte3"));
    
    auto cte1 = cache.get("cte1");
    auto cte2 = cache.get("cte2");
    auto cte3 = cache.get("cte3");
    
    ASSERT_TRUE(cte1.has_value());
    ASSERT_TRUE(cte2.has_value());
    ASSERT_TRUE(cte3.has_value());
    
    EXPECT_EQ(cte1->size(), 500);
    EXPECT_EQ(cte2->size(), 500);
    EXPECT_EQ(cte3->size(), 500);
}

TEST_F(CTECacheTest, SpillFileCleanup) {
    {
        CTECache::Config config;
        config.max_memory_bytes = 10 * 1024; // 10KB to force spill
        config.spill_directory = "./test_cte_spill";
        config.auto_cleanup = true;
        
        CTECache cache(config);
        
        // Create spill files
        ASSERT_TRUE(cache.store("spill1", createTestData(200)));
        ASSERT_TRUE(cache.store("spill2", createTestData(200)));
        
        EXPECT_TRUE(std::filesystem::exists("./test_cte_spill"));
        
        // Cache destructor should clean up files
    }
    
    // After cache is destroyed, spill directory should be gone
    EXPECT_FALSE(std::filesystem::exists("./test_cte_spill"));
}

// ============================================================================
// Phase 4.3: Memory Management Tests
// ============================================================================

TEST_F(CTECacheTest, MemoryUsageTracking) {
    CTECache::Config config;
    config.max_memory_bytes = 10 * 1024 * 1024; // 10MB
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    EXPECT_EQ(cache.getCurrentMemoryUsage(), 0);
    
    auto data = createTestData(100);
    ASSERT_TRUE(cache.store("test_cte", data));
    
    EXPECT_GT(cache.getCurrentMemoryUsage(), 0);
    
    cache.remove("test_cte");
    EXPECT_EQ(cache.getCurrentMemoryUsage(), 0);
}

TEST_F(CTECacheTest, ClearCache) {
    CTECache::Config config;
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    ASSERT_TRUE(cache.store("cte1", createTestData(50)));
    ASSERT_TRUE(cache.store("cte2", createTestData(50)));
    ASSERT_TRUE(cache.store("cte3", createTestData(50)));
    
    EXPECT_EQ(cache.size(), 3);
    EXPECT_GT(cache.getCurrentMemoryUsage(), 0);
    
    cache.clear();
    
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.getCurrentMemoryUsage(), 0);
    EXPECT_FALSE(cache.contains("cte1"));
    EXPECT_FALSE(cache.contains("cte2"));
    EXPECT_FALSE(cache.contains("cte3"));
}

TEST_F(CTECacheTest, StatsAccumulation) {
    CTECache::Config config;
    config.max_memory_bytes = 50 * 1024; // Small to force spills
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    // Store and retrieve to accumulate stats
    ASSERT_TRUE(cache.store("cte1", createTestData(500)));
    ASSERT_TRUE(cache.store("cte2", createTestData(500)));
    
    auto stats1 = cache.getStats();
    
    // Trigger disk reads
    auto cte1 = cache.get("cte1");
    auto cte2 = cache.get("cte2");
    
    auto stats2 = cache.getStats();
    
    EXPECT_GE(stats2.disk_reads, stats1.disk_reads);
    EXPECT_EQ(stats2.total_ctes, 2);
    EXPECT_EQ(stats2.total_results, 1000);
}

// ============================================================================
// Phase 4.3: Edge Cases
// ============================================================================

TEST_F(CTECacheTest, EmptyResults) {
    CTECache::Config config;
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    std::vector<json> empty_data;
    ASSERT_TRUE(cache.store("empty_cte", empty_data));
    
    auto retrieved = cache.get("empty_cte");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), 0);
}

TEST_F(CTECacheTest, NonExistentCTE) {
    CTECache::Config config;
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    auto result = cache.get("nonexistent");
    EXPECT_FALSE(result.has_value());
    
    EXPECT_FALSE(cache.contains("nonexistent"));
    EXPECT_FALSE(cache.remove("nonexistent"));
}

TEST_F(CTECacheTest, OverwriteCTE) {
    CTECache::Config config;
    config.spill_directory = "./test_cte_spill";
    config.auto_cleanup = true;
    
    CTECache cache(config);
    
    auto data1 = createTestData(50);
    ASSERT_TRUE(cache.store("overwrite_cte", data1));
    
    auto data2 = createTestData(100);
    ASSERT_TRUE(cache.store("overwrite_cte", data2)); // Overwrite
    
    auto retrieved = cache.get("overwrite_cte");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->size(), 100); // Should have new data
}
