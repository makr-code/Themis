// Test: Consistent Hash Distribution Quality
// Validates that consistent hashing provides good load distribution

#include <gtest/gtest.h>
#include "sharding/consistent_hash.h"
#include <map>
#include <cmath>
#include <vector>
#include <algorithm>

using namespace themis::sharding;

class ConsistentHashDistributionTest : public ::testing::Test {
protected:
    // Helper to calculate standard deviation
    double calculateStdDev(const std::vector<int>& values) {
        if (values.empty()) return 0.0;
        
        double sum = 0.0;
        for (int v : values) {
            sum += v;
        }
        double mean = sum / values.size();
        
        double variance = 0.0;
        for (int v : values) {
            double diff = v - mean;
            variance += diff * diff;
        }
        variance /= values.size();
        
        return std::sqrt(variance);
    }
};

// ===== Basic Distribution Tests =====

TEST_F(ConsistentHashDistributionTest, UniformDistribution) {
    const int num_shards = 10;
    const int num_keys = 10000;
    const int virtual_nodes = 150;
    
    ConsistentHash hash(num_shards);
    
    // Add shards
    for (int i = 0; i < num_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
        hash.addNode(shard_id, endpoint, virtual_nodes);
    }
    
    // Distribute keys
    std::map<std::string, int> key_counts;
    for (int i = 0; i < num_keys; i++) {
        std::string key = "key_" + std::to_string(i);
        auto shard_info = hash.getNode(key);
        
        ASSERT_TRUE(shard_info.has_value());
        key_counts[shard_info->shard_id]++;
    }
    
    // Check distribution
    EXPECT_EQ(key_counts.size(), num_shards);
    
    std::vector<int> counts;
    for (const auto& [shard, count] : key_counts) {
        counts.push_back(count);
    }
    
    double expected_per_shard = static_cast<double>(num_keys) / num_shards;
    double std_dev = calculateStdDev(counts);
    double cv = std_dev / expected_per_shard; // Coefficient of variation
    
    // CV should be low (< 0.15 for good distribution)
    EXPECT_LT(cv, 0.15) << "Distribution too uneven, CV = " << cv;
    
    // Each shard should get roughly equal keys (within 20%)
    for (int count : counts) {
        double deviation = std::abs(count - expected_per_shard) / expected_per_shard;
        EXPECT_LT(deviation, 0.20) << "Shard has " << count << " keys, expected ~" << expected_per_shard;
    }
}

// ===== Virtual Nodes Impact Tests =====

TEST_F(ConsistentHashDistributionTest, VirtualNodesImproveDistribution) {
    const int num_shards = 5;
    const int num_keys = 5000;
    
    std::vector<int> virtual_node_counts = {10, 50, 150, 300};
    std::vector<double> cvs;
    
    for (int virtual_nodes : virtual_node_counts) {
        ConsistentHash hash(num_shards);
        
        for (int i = 0; i < num_shards; i++) {
            std::string shard_id = "shard_" + std::to_string(i);
            std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
            hash.addNode(shard_id, endpoint, virtual_nodes);
        }
        
        std::map<std::string, int> key_counts;
        for (int i = 0; i < num_keys; i++) {
            std::string key = "key_" + std::to_string(i);
            auto shard_info = hash.getNode(key);
            key_counts[shard_info->shard_id]++;
        }
        
        std::vector<int> counts;
        for (const auto& [shard, count] : key_counts) {
            counts.push_back(count);
        }
        
        double expected = static_cast<double>(num_keys) / num_shards;
        double std_dev = calculateStdDev(counts);
        double cv = std_dev / expected;
        
        cvs.push_back(cv);
    }
    
    // More virtual nodes should improve distribution (lower CV)
    for (size_t i = 1; i < cvs.size(); i++) {
        EXPECT_LE(cvs[i], cvs[i-1] * 1.1) // Allow 10% tolerance
            << "More virtual nodes did not improve distribution";
    }
}

// ===== Node Addition/Removal Tests =====

TEST_F(ConsistentHashDistributionTest, MinimalKeyMigrationOnNodeAddition) {
    const int initial_shards = 5;
    const int num_keys = 10000;
    const int virtual_nodes = 150;
    
    ConsistentHash hash(initial_shards + 1);
    
    // Add initial shards
    for (int i = 0; i < initial_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
        hash.addNode(shard_id, endpoint, virtual_nodes);
    }
    
    // Record initial distribution
    std::map<std::string, std::string> initial_mapping;
    for (int i = 0; i < num_keys; i++) {
        std::string key = "key_" + std::to_string(i);
        auto shard_info = hash.getNode(key);
        initial_mapping[key] = shard_info->shard_id;
    }
    
    // Add new shard
    hash.addNode("shard_new", "http://shard_new.example.com", virtual_nodes);
    
    // Check how many keys migrated
    int keys_migrated = 0;
    std::map<std::string, int> new_distribution;
    
    for (int i = 0; i < num_keys; i++) {
        std::string key = "key_" + std::to_string(i);
        auto shard_info = hash.getNode(key);
        
        new_distribution[shard_info->shard_id]++;
        
        if (initial_mapping[key] != shard_info->shard_id) {
            keys_migrated++;
        }
    }
    
    // Ideally, only 1/(N+1) keys should migrate
    double expected_migration_ratio = 1.0 / (initial_shards + 1);
    double actual_migration_ratio = static_cast<double>(keys_migrated) / num_keys;
    
    // Allow 50% tolerance
    EXPECT_LT(actual_migration_ratio, expected_migration_ratio * 1.5)
        << "Too many keys migrated: " << keys_migrated << " (" 
        << (actual_migration_ratio * 100) << "%)";
    
    EXPECT_GT(actual_migration_ratio, expected_migration_ratio * 0.5)
        << "Too few keys migrated (possibly not distributing to new shard)";
    
    // New shard should get roughly equal share
    int new_shard_count = new_distribution["shard_new"];
    double expected_per_shard = static_cast<double>(num_keys) / (initial_shards + 1);
    double deviation = std::abs(new_shard_count - expected_per_shard) / expected_per_shard;
    
    EXPECT_LT(deviation, 0.30) 
        << "New shard distribution poor: " << new_shard_count << " keys, expected ~" << expected_per_shard;
}

TEST_F(ConsistentHashDistributionTest, MinimalKeyMigrationOnNodeRemoval) {
    const int initial_shards = 6;
    const int num_keys = 10000;
    const int virtual_nodes = 150;
    
    ConsistentHash hash(initial_shards);
    
    // Add all shards
    for (int i = 0; i < initial_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
        hash.addNode(shard_id, endpoint, virtual_nodes);
    }
    
    // Record initial distribution
    std::map<std::string, std::string> initial_mapping;
    std::map<std::string, int> initial_distribution;
    
    for (int i = 0; i < num_keys; i++) {
        std::string key = "key_" + std::to_string(i);
        auto shard_info = hash.getNode(key);
        initial_mapping[key] = shard_info->shard_id;
        initial_distribution[shard_info->shard_id]++;
    }
    
    // Remove one shard
    std::string removed_shard = "shard_2";
    int removed_shard_keys = initial_distribution[removed_shard];
    hash.removeNode(removed_shard);
    
    // Check redistribution
    int keys_migrated = 0;
    std::map<std::string, int> new_distribution;
    
    for (int i = 0; i < num_keys; i++) {
        std::string key = "key_" + std::to_string(i);
        auto shard_info = hash.getNode(key);
        
        new_distribution[shard_info->shard_id]++;
        
        if (initial_mapping[key] != shard_info->shard_id) {
            keys_migrated++;
        }
    }
    
    // Only keys from removed shard should migrate
    EXPECT_EQ(keys_migrated, removed_shard_keys)
        << "Only keys from removed shard should migrate";
    
    // Removed shard should not appear in new distribution
    EXPECT_EQ(new_distribution.count(removed_shard), 0);
    
    // Remaining shards should have relatively balanced distribution
    std::vector<int> counts;
    for (const auto& [shard, count] : new_distribution) {
        counts.push_back(count);
    }
    
    double expected = static_cast<double>(num_keys) / (initial_shards - 1);
    for (int count : counts) {
        double deviation = std::abs(count - expected) / expected;
        EXPECT_LT(deviation, 0.25);
    }
}

// ===== Determinism Tests =====

TEST_F(ConsistentHashDistributionTest, DeterministicMapping) {
    const int num_shards = 5;
    const int num_keys = 1000;
    const int virtual_nodes = 150;
    
    // Create two identical hash rings
    ConsistentHash hash1(num_shards);
    ConsistentHash hash2(num_shards);
    
    for (int i = 0; i < num_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
        hash1.addNode(shard_id, endpoint, virtual_nodes);
        hash2.addNode(shard_id, endpoint, virtual_nodes);
    }
    
    // Same keys should map to same shards
    for (int i = 0; i < num_keys; i++) {
        std::string key = "key_" + std::to_string(i);
        
        auto shard1 = hash1.getNode(key);
        auto shard2 = hash2.getNode(key);
        
        ASSERT_TRUE(shard1.has_value());
        ASSERT_TRUE(shard2.has_value());
        EXPECT_EQ(shard1->shard_id, shard2->shard_id)
            << "Same key mapped to different shards in identical rings";
    }
}

// ===== Load Balance Tests =====

TEST_F(ConsistentHashDistributionTest, BalancedLoadAcrossDifferentShardCounts) {
    const int num_keys = 10000;
    const int virtual_nodes = 150;
    
    std::vector<int> shard_counts = {3, 5, 10, 20};
    
    for (int num_shards : shard_counts) {
        ConsistentHash hash(num_shards);
        
        for (int i = 0; i < num_shards; i++) {
            std::string shard_id = "shard_" + std::to_string(i);
            std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
            hash.addNode(shard_id, endpoint, virtual_nodes);
        }
        
        std::map<std::string, int> key_counts;
        for (int i = 0; i < num_keys; i++) {
            std::string key = "key_" + std::to_string(i);
            auto shard_info = hash.getNode(key);
            key_counts[shard_info->shard_id]++;
        }
        
        std::vector<int> counts;
        for (const auto& [shard, count] : key_counts) {
            counts.push_back(count);
        }
        
        double expected = static_cast<double>(num_keys) / num_shards;
        double std_dev = calculateStdDev(counts);
        double cv = std_dev / expected;
        
        EXPECT_LT(cv, 0.20) 
            << "Poor distribution with " << num_shards << " shards, CV = " << cv;
    }
}

// ===== Special Key Patterns Tests =====

TEST_F(ConsistentHashDistributionTest, SequentialKeysDistribution) {
    const int num_shards = 5;
    const int num_keys = 10000;
    const int virtual_nodes = 150;
    
    ConsistentHash hash(num_shards);
    
    for (int i = 0; i < num_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
        hash.addNode(shard_id, endpoint, virtual_nodes);
    }
    
    // Sequential keys (common pattern)
    std::map<std::string, int> key_counts;
    for (int i = 0; i < num_keys; i++) {
        std::string key = std::to_string(i); // Just numbers
        auto shard_info = hash.getNode(key);
        key_counts[shard_info->shard_id]++;
    }
    
    std::vector<int> counts;
    for (const auto& [shard, count] : key_counts) {
        counts.push_back(count);
    }
    
    double expected = static_cast<double>(num_keys) / num_shards;
    double std_dev = calculateStdDev(counts);
    double cv = std_dev / expected;
    
    // Should still distribute well even with sequential keys
    EXPECT_LT(cv, 0.20) 
        << "Sequential keys not well distributed, CV = " << cv;
}

TEST_F(ConsistentHashDistributionTest, PrefixedKeysDistribution) {
    const int num_shards = 5;
    const int num_prefixes = 10;
    const int keys_per_prefix = 1000;
    const int virtual_nodes = 150;
    
    ConsistentHash hash(num_shards);
    
    for (int i = 0; i < num_shards; i++) {
        std::string shard_id = "shard_" + std::to_string(i);
        std::string endpoint = "http://shard" + std::to_string(i) + ".example.com";
        hash.addNode(shard_id, endpoint, virtual_nodes);
    }
    
    // Keys with common prefixes
    std::map<std::string, int> key_counts;
    for (int p = 0; p < num_prefixes; p++) {
        std::string prefix = "prefix_" + std::to_string(p) + "_";
        
        for (int i = 0; i < keys_per_prefix; i++) {
            std::string key = prefix + std::to_string(i);
            auto shard_info = hash.getNode(key);
            key_counts[shard_info->shard_id]++;
        }
    }
    
    std::vector<int> counts;
    for (const auto& [shard, count] : key_counts) {
        counts.push_back(count);
    }
    
    int total_keys = num_prefixes * keys_per_prefix;
    double expected = static_cast<double>(total_keys) / num_shards;
    double std_dev = calculateStdDev(counts);
    double cv = std_dev / expected;
    
    EXPECT_LT(cv, 0.20) 
        << "Prefixed keys not well distributed, CV = " << cv;
}
