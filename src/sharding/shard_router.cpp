#include "sharding/shard_router.h"
#include <algorithm>
#include <regex>

namespace themis::sharding {

ShardRouter::ShardRouter(
    std::shared_ptr<URNResolver> resolver,
    std::shared_ptr<RemoteExecutor> executor,
    const Config& config)
    : resolver_(resolver),
      executor_(executor),
      config_(config) {
}

std::optional<nlohmann::json> ShardRouter::get(const URN& urn) {
    total_requests_++;
    
    auto result = routeRequest(urn, "GET", "/api/v1/data/" + urn.toString());
    
    if (result.success) {
        return result.data;
    }
    
    errors_++;
    return std::nullopt;
}

bool ShardRouter::put(const URN& urn, const nlohmann::json& data) {
    total_requests_++;
    
    auto result = routeRequest(urn, "PUT", "/api/v1/data/" + urn.toString(), std::optional<nlohmann::json>(data));
    
    if (!result.success) {
        errors_++;
    }
    
    return result.success;
}

bool ShardRouter::del(const URN& urn) {
    total_requests_++;
    
    auto result = routeRequest(urn, "DELETE", "/api/v1/data/" + urn.toString());
    
    if (!result.success) {
        errors_++;
    }
    
    return result.success;
}

nlohmann::json ShardRouter::executeQuery(const std::string& query) {
    total_requests_++;
    
    // Analyze query to determine routing strategy
    RoutingStrategy strategy = analyzeQuery(query);
    
    switch (strategy) {
        case RoutingStrategy::SINGLE_SHARD: {
            // Extract URN and route to single shard
            auto urn = extractURN(query);
            if (urn) {
                auto result = routeRequest(*urn, "POST", "/api/v1/query", std::optional<nlohmann::json>(nlohmann::json{{"query", query}}));
                return result.data;
            }
            
            // Fallback to scatter-gather if URN not found
            [[fallthrough]];
        }
        
        case RoutingStrategy::SCATTER_GATHER: {
            scatter_gather_requests_++;
            auto results = scatterGather(query);
            return mergeResults(results);
        }
        
        case RoutingStrategy::NAMESPACE_LOCAL: {
            // Similar to scatter-gather but only to shards in namespace
            // For Phase 3, treat as scatter-gather
            scatter_gather_requests_++;
            auto results = scatterGather(query);
            return mergeResults(results);
        }
        
        case RoutingStrategy::CROSS_SHARD_JOIN: {
            // For Phase 3, treat as scatter-gather
            scatter_gather_requests_++;
            auto results = scatterGather(query);
            return mergeResults(results);
        }
    }
    
    return nlohmann::json{};
}

RoutingStrategy ShardRouter::analyzeQuery(const std::string& query) const {
    // Simple query analysis
    // In production, would parse AQL/SQL and analyze
    
    // Check for URN in query
    if (query.find("urn:themis:") != std::string::npos) {
        return RoutingStrategy::SINGLE_SHARD;
    }
    
    // Check for JOIN keyword
    if (query.find("JOIN") != std::string::npos || 
        query.find("join") != std::string::npos) {
        return RoutingStrategy::CROSS_SHARD_JOIN;
    }
    
    // Check for namespace specification
    if (query.find("NAMESPACE") != std::string::npos) {
        return RoutingStrategy::NAMESPACE_LOCAL;
    }
    
    // Default to scatter-gather for full scans
    return RoutingStrategy::SCATTER_GATHER;
}

std::vector<ShardResult> ShardRouter::scatterGather(const std::string& query) {
    std::vector<ShardResult> results;
    
    // Get all healthy shards
    auto shards = resolver_->getHealthyShards();
    
    // Execute query on each shard
    for (const auto& shard : shards) {
        ShardResult result;
        result.shard_id = shard.shard_id;
        
        // Check if this is the local shard
        if (shard.shard_id == config_.local_shard_id) {
            local_requests_++;
            result = executeLocal("POST", "/api/v1/query", std::optional<nlohmann::json>(nlohmann::json{{"query", query}}));
        } else {
            remote_requests_++;
            auto exec_result = executor_->executeQuery(shard, query);
            
            result.success = exec_result.success;
            result.data = exec_result.data;
            result.error_msg = exec_result.error;
            result.execution_time_ms = exec_result.execution_time_ms;
        }
        
        results.push_back(result);
    }
    
    return results;
}

nlohmann::json ShardRouter::executeCrossShardJoin(
    const std::string& query,
    const std::string& join_field) {
    (void)join_field; // Future: implement join logic based on field
    
    // Simplified cross-shard join
    // Phase 1: Execute query on all shards
    auto results = scatterGather(query);
    
    // Phase 2: Merge results (simplified - full join would require lookup phase)
    return mergeResults(results);
}

nlohmann::json ShardRouter::getStatistics() const {
    return nlohmann::json{
        {"total_requests", total_requests_.load()},
        {"local_requests", local_requests_.load()},
        {"remote_requests", remote_requests_.load()},
        {"scatter_gather_requests", scatter_gather_requests_.load()},
        {"errors", errors_.load()}
    };
}

ShardResult ShardRouter::routeRequest(
    const URN& urn,
    const std::string& method,
    const std::string& path,
    const std::optional<nlohmann::json>& body) {
    
    ShardResult result;
    
    // Resolve URN to shard
    auto shard_info = resolver_->resolvePrimary(urn);
    if (!shard_info) {
        result.success = false;
        result.error_msg = "Failed to resolve URN to shard";
        errors_++;
        return result;
    }
    
    result.shard_id = shard_info->shard_id;
    
    // Check if local
    if (resolver_->isLocal(urn)) {
        local_requests_++;
        return executeLocal(method, path, body);
    }
    
    // Execute remotely
    remote_requests_++;
    RemoteExecutor::Result exec_result;
    
    if (method == "GET") {
        exec_result = executor_->get(*shard_info, path);
    } else if (method == "PUT" && body) {
        exec_result = executor_->put(*shard_info, path, *body);
    } else if (method == "DELETE") {
        exec_result = executor_->del(*shard_info, path);
    } else if (method == "POST" && body) {
        exec_result = executor_->post(*shard_info, path, *body);
    }
    
    result.success = exec_result.success;
    result.data = exec_result.data;
    result.error_msg = exec_result.error;
    result.execution_time_ms = exec_result.execution_time_ms;
    
    return result;
}

ShardResult ShardRouter::executeLocal(
    const std::string& method,
    const std::string& path,
    const std::optional<nlohmann::json>& body) {
    
    ShardResult result;
    result.shard_id = config_.local_shard_id;
    result.success = true;
    result.execution_time_ms = 0;
    
    // For Phase 3, return placeholder response
    // In production, would execute against local storage
    result.data = nlohmann::json{
        {"local_execution", true},
        {"method", method},
        {"path", path}
    };
    
    if (body) {
        result.data["body"] = *body;
    }
    
    return result;
}

nlohmann::json ShardRouter::mergeResults(const std::vector<ShardResult>& results) {
    nlohmann::json merged;
    merged["results"] = nlohmann::json::array();
    merged["errors"] = nlohmann::json::array();
    merged["shard_count"] = results.size();
    
    size_t success_count = 0;
    
    for (const auto& result : results) {
        if (result.success) {
            success_count++;
            
            // If result has data array, merge it
            if (result.data.is_array()) {
                for (const auto& item : result.data) {
                    merged["results"].push_back(item);
                }
            } else if (result.data.contains("results") && result.data["results"].is_array()) {
                for (const auto& item : result.data["results"]) {
                    merged["results"].push_back(item);
                }
            } else {
                // Single result
                merged["results"].push_back(result.data);
            }
        } else {
            merged["errors"].push_back(nlohmann::json{
                {"shard_id", result.shard_id},
                {"error", result.error_msg}
            });
        }
    }
    
    merged["success_count"] = success_count;
    merged["error_count"] = results.size() - success_count;
    
    return merged;
}

nlohmann::json ShardRouter::applyPagination(
    const nlohmann::json& merged,
    size_t offset,
    size_t limit) {
    
    nlohmann::json paginated = merged;
    
    if (merged.contains("results") && merged["results"].is_array()) {
        const auto& results = merged["results"];
        nlohmann::json page = nlohmann::json::array();
        
        size_t start = std::min(offset, results.size());
        size_t end = std::min(start + limit, results.size());
        
        for (size_t i = start; i < end; ++i) {
            page.push_back(results[i]);
        }
        
        paginated["results"] = page;
        paginated["offset"] = offset;
        paginated["limit"] = limit;
        paginated["total_count"] = results.size();
    }
    
    return paginated;
}

std::optional<URN> ShardRouter::extractURN(const std::string& query) const {
    // Simple regex to find URN in query
    std::regex urn_pattern(R"(urn:themis:[^:]+:[^:]+:[^:]+:[a-f0-9-]+)");
    std::smatch match;
    
    if (std::regex_search(query, match, urn_pattern)) {
        return URN::parse(match[0].str());
    }
    
    return std::nullopt;
}

std::optional<std::string> ShardRouter::extractNamespace(const std::string& query) const {
    // Simple pattern matching for namespace
    std::regex ns_pattern(R"(NAMESPACE\s+([a-zA-Z0-9_]+))");
    std::smatch match;
    
    if (std::regex_search(query, match, ns_pattern)) {
        return match[1].str();
    }
    
    return std::nullopt;
}

} // namespace themis::sharding
