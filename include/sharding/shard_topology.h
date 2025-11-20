#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <map>
#include <optional>
#include <algorithm>

namespace themis::sharding {

/**
 * Shard Information
 * Contains all metadata about a shard including network endpoints,
 * location, and health status.
 */
struct ShardInfo {
    std::string shard_id;                    // shard_001, shard_002, ...
    std::string primary_endpoint;            // themis-shard001.dc1.example.com:8080
    std::vector<std::string> replica_endpoints; // replica nodes
    std::string datacenter;                  // dc1, dc2, us-east-1, eu-west-1
    std::string rack;                        // rack01, rack02 (locality awareness)
    uint64_t token_start;                    // Consistent Hash Range Start
    uint64_t token_end;                      // Consistent Hash Range End
    bool is_healthy;                         // Health check status
    
    // PKI/Security fields
    std::string certificate_serial;          // X.509 certificate serial number
    std::vector<std::string> capabilities;   // read, write, replicate, admin
    
    /**
     * Check if this shard has a specific capability
     */
    bool hasCapability(const std::string& cap) const {
        return std::find(capabilities.begin(), capabilities.end(), cap) != capabilities.end();
    }
};

/**
 * Shard Topology Manager
 * 
 * Manages the cluster topology including shard locations, health status,
 * and metadata. Integrates with metadata store (etcd) for distributed
 * configuration.
 * 
 * Thread-safe for concurrent access.
 */
class ShardTopology {
public:
    /**
     * Configuration for ShardTopology
     */
    struct Config {
        std::string metadata_endpoint;  // etcd endpoint (e.g., "http://localhost:2379")
        std::string cluster_name;       // Cluster identifier
        uint32_t refresh_interval_sec;  // Auto-refresh interval (0 = manual only)
        bool enable_health_checks;      // Enable periodic health checks
    };
    
    /**
     * Construct ShardTopology with configuration
     * @param config Configuration parameters
     */
    explicit ShardTopology(const Config& config);
    
    /**
     * Add or update shard information
     * @param shard Shard information
     */
    void addShard(const ShardInfo& shard);
    
    /**
     * Remove shard from topology
     * @param shard_id Shard identifier
     */
    void removeShard(const std::string& shard_id);
    
    /**
     * Get shard information by ID
     * @param shard_id Shard identifier
     * @return ShardInfo if found, nullopt otherwise
     */
    std::optional<ShardInfo> getShard(const std::string& shard_id) const;
    
    /**
     * Get all shards in the cluster
     * @return Vector of all shard information
     */
    std::vector<ShardInfo> getAllShards() const;
    
    /**
     * Get healthy shards only
     * @return Vector of healthy shards
     */
    std::vector<ShardInfo> getHealthyShards() const;
    
    /**
     * Update shard health status
     * @param shard_id Shard identifier
     * @param is_healthy Health status
     */
    void updateHealth(const std::string& shard_id, bool is_healthy);
    
    /**
     * Refresh topology from metadata store
     * Loads latest shard configuration from etcd
     */
    void refresh();
    
    /**
     * Save topology to metadata store
     * Persists current topology to etcd
     */
    void save();
    
    /**
     * Get number of shards
     * @return Total shard count
     */
    size_t getShardCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shards_.size();
    }
    
    /**
     * Check if a shard exists
     * @param shard_id Shard identifier
     * @return true if shard exists
     */
    bool hasShard(const std::string& shard_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shards_.find(shard_id) != shards_.end();
    }
    
    /**
     * Clear all shards (for testing)
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        shards_.clear();
    }
    
private:
    Config config_;
    std::map<std::string, ShardInfo> shards_;
    mutable std::mutex mutex_;
    
    /**
     * Load topology from metadata store (etcd)
     * Internal method called by refresh()
     */
    void loadFromMetadataStore();
    
    /**
     * Save topology to metadata store (etcd)
     * Internal method called by save()
     */
    void saveToMetadataStore();
};

} // namespace themis::sharding
