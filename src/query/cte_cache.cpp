// Phase 4.3: CTE Memory Management Implementation
#include "query/cte_cache.h"
#include "utils/logger.h"
#include <algorithm>
#include <cstring>

namespace themis {
namespace query {

CTECache::CTECache() : CTECache(Config{}) {}

CTECache::CTECache(Config config)
    : config_(std::move(config)) {
    if (config_.auto_cleanup) {
        ensureSpillDirectory();
    }
}

CTECache::~CTECache() {
    if (config_.auto_cleanup) {
        // Clean up spill files
        try {
            for (const auto& [name, entry] : entries_) {
                if (entry.is_spilled && !entry.spill_file_path.empty()) {
                    std::filesystem::remove(entry.spill_file_path);
                }
            }
            // Remove spill directory if empty
            if (std::filesystem::exists(config_.spill_directory) &&
                std::filesystem::is_empty(config_.spill_directory)) {
                std::filesystem::remove(config_.spill_directory);
            }
        } catch (const std::exception& e) {
            THEMIS_WARN("Failed to cleanup CTE spill files: {}", e.what());
        }
    }
}

bool CTECache::store(const std::string& name, std::vector<nlohmann::json> results) {
    // Estimate size of results
    size_t estimated_size = estimateSize(results);
    
    // Check if we need to make room
    if (current_memory_usage_ + estimated_size > config_.max_memory_bytes) {
        // Try to make room by spilling existing CTEs
        if (!makeRoom(estimated_size)) {
            // If we can't make enough room, spill this CTE directly to disk
            THEMIS_DEBUG("CTE '{}' too large for memory ({} bytes), spilling to disk", 
                        name, estimated_size);
            if (!spillToDisk(name, results)) {
                THEMIS_ERROR("Failed to spill CTE '{}' to disk", name);
                return false;
            }
            
            CacheEntry entry;
            entry.name = name;
            entry.result_count = results.size();
            entry.estimated_size_bytes = estimated_size;
            entry.is_spilled = true;
            entry.spill_file_path = getSpillFilePath(name);
            entries_[name] = std::move(entry);
            
            return true;
        }
    }
    
    // Store in memory
    CacheEntry entry;
    entry.name = name;
    entry.result_count = results.size();
    entry.estimated_size_bytes = estimated_size;
    entry.is_spilled = false;
    entry.in_memory_data = std::move(results);
    
    current_memory_usage_ += estimated_size;
    entries_[name] = std::move(entry);
    
    THEMIS_DEBUG("CTE '{}' cached in memory ({} results, {} bytes)", 
                name, entry.result_count, estimated_size);
    
    return true;
}

std::optional<std::vector<nlohmann::json>> CTECache::get(const std::string& name) {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        return std::nullopt;
    }
    
    auto& entry = it->second;
    
    if (entry.is_spilled) {
        // Load from disk
        stat_disk_reads_++;
        return loadFromDisk(name);
    } else {
        // Return in-memory data
        return entry.in_memory_data;
    }
}

bool CTECache::contains(const std::string& name) const {
    return entries_.find(name) != entries_.end();
}

void CTECache::remove(const std::string& name) {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        return;
    }
    
    auto& entry = it->second;
    
    if (entry.is_spilled && !entry.spill_file_path.empty()) {
        // Remove spill file
        try {
            std::filesystem::remove(entry.spill_file_path);
        } catch (const std::exception& e) {
            THEMIS_WARN("Failed to remove spill file for CTE '{}': {}", name, e.what());
        }
    } else {
        // Free memory
        current_memory_usage_ -= entry.estimated_size_bytes;
    }
    
    entries_.erase(it);
}

void CTECache::clear() {
    for (const auto& [name, entry] : entries_) {
        if (entry.is_spilled && !entry.spill_file_path.empty()) {
            try {
                std::filesystem::remove(entry.spill_file_path);
            } catch (...) {}
        }
    }
    
    entries_.clear();
    current_memory_usage_ = 0;
    stat_spill_operations_ = 0;
    stat_disk_reads_ = 0;
}

CTECache::Stats CTECache::getStats() const {
    Stats stats;
    stats.total_ctes = entries_.size();
    stats.memory_usage_bytes = current_memory_usage_;
    stats.spill_operations = stat_spill_operations_;
    stats.disk_reads = stat_disk_reads_;
    
    for (const auto& [name, entry] : entries_) {
        if (entry.is_spilled) {
            stats.spilled_ctes++;
        } else {
            stats.in_memory_ctes++;
        }
        stats.total_results += entry.result_count;
    }
    
    return stats;
}

size_t CTECache::estimateSize(const std::vector<nlohmann::json>& data) const {
    if (data.empty()) {
        return 0;
    }
    
    // Sample-based estimation: Check first few elements and extrapolate
    constexpr size_t SAMPLE_SIZE = 10;
    size_t sample_count = std::min(SAMPLE_SIZE, data.size());
    size_t sample_bytes = 0;
    
    for (size_t i = 0; i < sample_count; i++) {
        // Serialize to get accurate byte size
        std::string serialized = data[i].dump();
        sample_bytes += serialized.size();
    }
    
    // Extrapolate to full dataset
    size_t avg_bytes_per_element = sample_bytes / sample_count;
    size_t total_estimate = avg_bytes_per_element * data.size();
    
    // Add overhead for vector structure (rough estimate)
    total_estimate += sizeof(nlohmann::json) * data.size();
    
    return total_estimate;
}

bool CTECache::spillToDisk(const std::string& name, const std::vector<nlohmann::json>& data) {
    ensureSpillDirectory();
    
    std::string spill_path = getSpillFilePath(name);
    
    try {
        std::ofstream file(spill_path, std::ios::binary);
        if (!file.is_open()) {
            THEMIS_ERROR("Failed to open spill file: {}", spill_path);
            return false;
        }
        
        // Write result count
        size_t count = data.size();
        file.write(reinterpret_cast<const char*>(&count), sizeof(count));
        
        // Write each JSON document
        for (const auto& doc : data) {
            std::string serialized = doc.dump();
            size_t size = serialized.size();
            
            // Write size then data
            file.write(reinterpret_cast<const char*>(&size), sizeof(size));
            file.write(serialized.data(), size);
        }
        
        file.close();
        stat_spill_operations_++;
        
        THEMIS_DEBUG("Spilled CTE '{}' to disk: {} results to {}", 
                    name, count, spill_path);
        
        return true;
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to spill CTE '{}' to disk: {}", name, e.what());
        return false;
    }
}

std::optional<std::vector<nlohmann::json>> CTECache::loadFromDisk(const std::string& name) {
    auto it = entries_.find(name);
    if (it == entries_.end() || !it->second.is_spilled) {
        return std::nullopt;
    }
    
    std::string spill_path = it->second.spill_file_path;
    
    try {
        std::ifstream file(spill_path, std::ios::binary);
        if (!file.is_open()) {
            THEMIS_ERROR("Failed to open spill file: {}", spill_path);
            return std::nullopt;
        }
        
        // Read result count
        size_t count = 0;
        file.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        std::vector<nlohmann::json> results;
        results.reserve(count);
        
        // Read each JSON document
        for (size_t i = 0; i < count; i++) {
            size_t size = 0;
            file.read(reinterpret_cast<char*>(&size), sizeof(size));
            
            std::string serialized(size, '\0');
            file.read(&serialized[0], size);
            
            results.push_back(nlohmann::json::parse(serialized));
        }
        
        file.close();
        
        THEMIS_DEBUG("Loaded CTE '{}' from disk: {} results from {}", 
                    name, count, spill_path);
        
        return results;
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to load CTE '{}' from disk: {}", name, e.what());
        return std::nullopt;
    }
}

bool CTECache::makeRoom(size_t required_bytes) {
    // Find largest in-memory CTE to spill
    std::string largest_cte;
    size_t largest_size = 0;
    
    for (const auto& [name, entry] : entries_) {
        if (!entry.is_spilled && entry.estimated_size_bytes > largest_size) {
            largest_cte = name;
            largest_size = entry.estimated_size_bytes;
        }
    }
    
    // If no in-memory CTEs or largest isn't big enough, can't make room
    if (largest_cte.empty() || largest_size < required_bytes) {
        return false;
    }
    
    // Spill the largest CTE
    auto it = entries_.find(largest_cte);
    if (it == entries_.end()) {
        return false;
    }
    
    auto& entry = it->second;
    
    THEMIS_DEBUG("Making room by spilling CTE '{}' ({} bytes)", 
                largest_cte, entry.estimated_size_bytes);
    
    if (!spillToDisk(largest_cte, entry.in_memory_data)) {
        return false;
    }
    
    // Update entry
    current_memory_usage_ -= entry.estimated_size_bytes;
    entry.is_spilled = true;
    entry.spill_file_path = getSpillFilePath(largest_cte);
    entry.in_memory_data.clear();
    entry.in_memory_data.shrink_to_fit();
    
    return true;
}

std::string CTECache::getSpillFilePath(const std::string& name) const {
    // Sanitize CTE name for filename
    std::string safe_name = name;
    for (char& c : safe_name) {
        if (!std::isalnum(c) && c != '_') {
            c = '_';
        }
    }
    
    return config_.spill_directory + "/cte_" + safe_name + ".bin";
}

void CTECache::ensureSpillDirectory() {
    try {
        if (!std::filesystem::exists(config_.spill_directory)) {
            std::filesystem::create_directories(config_.spill_directory);
        }
    } catch (const std::exception& e) {
        THEMIS_WARN("Failed to create spill directory: {}", e.what());
    }
}

} // namespace query
} // namespace themis
