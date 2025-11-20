// Phase 4.3: CTE Memory Management and Spill-to-Disk
#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <optional>
#include <fstream>
#include <filesystem>

namespace themis {
namespace query {

/**
 * @brief CTE Result Cache with Memory Management
 * 
 * Manages CTE results with automatic spill-to-disk when memory threshold is exceeded.
 * Provides transparent access to cached results regardless of storage location.
 */
class CTECache {
public:
    /**
     * @brief Configuration for cache behavior
     */
    struct Config {
        size_t max_memory_bytes = 100 * 1024 * 1024;  // 100 MB default
        std::string spill_directory = "./themis_cte_spill";
        bool enable_compression = true;
        bool auto_cleanup = true;  // Delete spill files on destruction
    };
    
    /**
     * @brief Metadata about a cached CTE
     */
    struct CacheEntry {
        std::string name;
        size_t result_count = 0;
        size_t estimated_size_bytes = 0;
        bool is_spilled = false;
        std::string spill_file_path;
        std::vector<nlohmann::json> in_memory_data;  // Only populated if not spilled
    };
    
    /**
     * @brief Construct cache with configuration
     */
    CTECache();
    explicit CTECache(Config config);
    
    /**
     * @brief Destructor - cleanup spill files if auto_cleanup enabled
     */
    ~CTECache();
    
    // Delete copy constructor and assignment (cache is not copyable)
    CTECache(const CTECache&) = delete;
    CTECache& operator=(const CTECache&) = delete;
    
    // Allow move
    CTECache(CTECache&&) noexcept = default;
    CTECache& operator=(CTECache&&) noexcept = default;
    
    /**
     * @brief Store CTE results in cache
     * 
     * Automatically decides whether to keep in memory or spill to disk
     * based on current memory usage and result size.
     * 
     * @param name CTE name
     * @param results CTE result set
     * @return true if stored successfully
     */
    bool store(const std::string& name, std::vector<nlohmann::json> results);
    
    /**
     * @brief Retrieve CTE results from cache
     * 
     * Transparently loads from disk if spilled.
     * 
     * @param name CTE name
     * @return CTE results or nullopt if not found
     */
    std::optional<std::vector<nlohmann::json>> get(const std::string& name);
    
    /**
     * @brief Check if CTE exists in cache
     */
    bool contains(const std::string& name) const;
    
    /**
     * @brief Remove CTE from cache
     */
    void remove(const std::string& name);
    
    /**
     * @brief Clear all cached CTEs
     */
    void clear();
    
    /**
     * @brief Get current memory usage in bytes
     */
    size_t getCurrentMemoryUsage() const { return current_memory_usage_; }
    
    /**
     * @brief Get number of cached CTEs
     */
    size_t size() const { return entries_.size(); }
    
    /**
     * @brief Get cache statistics
     */
    struct Stats {
        size_t total_ctes = 0;
        size_t in_memory_ctes = 0;
        size_t spilled_ctes = 0;
        size_t memory_usage_bytes = 0;
        size_t total_results = 0;
        size_t spill_operations = 0;
        size_t disk_reads = 0;
    };
    Stats getStats() const;
    
private:
    /**
     * @brief Estimate memory size of JSON array
     */
    size_t estimateSize(const std::vector<nlohmann::json>& data) const;
    
    /**
     * @brief Spill CTE to disk
     */
    bool spillToDisk(const std::string& name, const std::vector<nlohmann::json>& data);
    
    /**
     * @brief Load CTE from disk
     */
    std::optional<std::vector<nlohmann::json>> loadFromDisk(const std::string& name);
    
    /**
     * @brief Make room by spilling largest in-memory CTE
     */
    bool makeRoom(size_t required_bytes);
    
    /**
     * @brief Generate spill file path for CTE
     */
    std::string getSpillFilePath(const std::string& name) const;
    
    /**
     * @brief Ensure spill directory exists
     */
    void ensureSpillDirectory();
    
    Config config_;
    std::unordered_map<std::string, CacheEntry> entries_;
    size_t current_memory_usage_ = 0;
    
    // Statistics
    mutable size_t stat_spill_operations_ = 0;
    mutable size_t stat_disk_reads_ = 0;
};

} // namespace query
} // namespace themis
