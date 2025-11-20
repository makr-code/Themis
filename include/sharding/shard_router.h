#pragma once

#include "sharding/urn_resolver.h"
#include "sharding/remote_executor.h"
#include <string>
#include <atomic>
#include <vector>
#include <optional>
#include <memory>
#include <nlohmann/json.hpp>

namespace themis::sharding {

/**
 * Query Routing Strategy
 */
enum class RoutingStrategy {
    SINGLE_SHARD,     // Query hits one shard (e.g., GET by URN)
    SCATTER_GATHER,   // Query spans all shards (e.g., full table scan)
    NAMESPACE_LOCAL,  // Query scoped to namespace (multi-shard but not all)
    CROSS_SHARD_JOIN  // Join across shards (expensive)
};

/**
 * Result from a shard
 */
struct ShardResult {
    std::string shard_id;           // Shard identifier
    nlohmann::json data;            // Result data
    bool success;                   // true if successful
    std::string error_msg;          // Error message if failed
    uint64_t execution_time_ms;     // Execution time
};

/**
 * Shard Router - Routes queries to appropriate shards
 * 
 * Responsible for:
 * - Determining which shard(s) to route to
 * - Executing single-shard operations
 * - Coordinating scatter-gather queries
 * - Merging results from multiple shards
 * - Handling cross-shard joins
 */
class ShardRouter {
public:
    /**
     * Configuration for Shard Router
     */
    struct Config {
        std::string local_shard_id;     // This shard's ID
        
        // Scatter-gather configuration
        uint32_t scatter_timeout_ms = 30000;    // Timeout for scatter-gather
        size_t max_concurrent_shards = 10;       // Max concurrent shard requests
        
        // Query optimization
        bool enable_query_pushdown = true;      // Push predicates to shards
        bool enable_result_caching = false;     // Cache query results
    };
    
    /**
     * Construct Shard Router
     * @param resolver URN resolver for shard location
     * @param executor Remote executor for shard communication
     * @param config Router configuration
     */
    ShardRouter(
        std::shared_ptr<URNResolver> resolver,
        std::shared_ptr<RemoteExecutor> executor,
        const Config& config
    );
    
    /**
     * Route GET request by URN
     * @param urn URN to retrieve
     * @return Data from shard, or nullopt if not found/error
     */
    std::optional<nlohmann::json> get(const URN& urn);
    
    /**
     * Route PUT request by URN
     * @param urn URN to update
     * @param data Data to store
     * @return true if successful
     */
    bool put(const URN& urn, const nlohmann::json& data);
    
    /**
     * Route DELETE request by URN
     * @param urn URN to delete
     * @return true if successful
     */
    bool del(const URN& urn);
    
    /**
     * Execute query (may span multiple shards)
     * @param query Query string (e.g., AQL query)
     * @return Combined results from all relevant shards
     */
    nlohmann::json executeQuery(const std::string& query);
    
    /**
     * Determine routing strategy for a query
     * Analyzes query to determine which shards to involve
     * @param query Query string
     * @return Routing strategy
     */
    RoutingStrategy analyzeQuery(const std::string& query) const;
    
    /**
     * Execute scatter-gather query
     * Sends query to all shards and merges results
     * @param query Query to execute
     * @return Merged results from all shards
     */
    std::vector<ShardResult> scatterGather(const std::string& query);
    
    /**
     * Execute cross-shard join (simplified two-phase approach)
     * Phase 1: Fetch from first collection
     * Phase 2: Lookup in second collection
     * @param query Query string
     * @param join_field Field to join on
     * @return Joined results
     */
    nlohmann::json executeCrossShardJoin(
        const std::string& query,
        const std::string& join_field
    );
    
    /**
     * Get statistics about routing
     * @return Statistics (requests routed, errors, etc.)
     */
    nlohmann::json getStatistics() const;

private:
    std::shared_ptr<URNResolver> resolver_;
    std::shared_ptr<RemoteExecutor> executor_;
    Config config_;
    
    // Statistics
    mutable std::atomic<uint64_t> total_requests_{0};
    mutable std::atomic<uint64_t> local_requests_{0};
    mutable std::atomic<uint64_t> remote_requests_{0};
    mutable std::atomic<uint64_t> scatter_gather_requests_{0};
    mutable std::atomic<uint64_t> errors_{0};
    
    /**
     * Route request to appropriate shard
     * Handles both local and remote execution
     * @param urn URN to route
     * @param method HTTP method
     * @param path Request path
     * @param body Optional request body
     * @return Result from shard
     */
    ShardResult routeRequest(
        const URN& urn,
        const std::string& method,
        const std::string& path,
        const std::optional<nlohmann::json>& body = std::nullopt
    );
    
    /**
     * Execute request locally (this shard)
     * @param path Request path
     * @param method HTTP method
     * @param body Optional request body
     * @return Result from local execution
     */
    ShardResult executeLocal(
        const std::string& method,
        const std::string& path,
        const std::optional<nlohmann::json>& body = std::nullopt
    );
    
    /**
     * Merge results from multiple shards
     * Combines data arrays and handles errors
     * @param results Results from shards
     * @return Merged result
     */
    nlohmann::json mergeResults(const std::vector<ShardResult>& results);
    
    /**
     * Apply LIMIT/OFFSET across shards
     * For scatter-gather queries
     * @param merged Merged results
     * @param offset Offset to apply
     * @param limit Limit to apply
     * @return Paginated results
     */
    nlohmann::json applyPagination(
        const nlohmann::json& merged,
        size_t offset,
        size_t limit
    );
    
    /**
     * Extract URN from query (if present)
     * Simple pattern matching for URN-based queries
     * @param query Query string
     * @return URN if found
     */
    std::optional<URN> extractURN(const std::string& query) const;
    
    /**
     * Extract namespace from query (if present)
     * @param query Query string
     * @return Namespace if found
     */
    std::optional<std::string> extractNamespace(const std::string& query) const;
};

} // namespace themis::sharding
