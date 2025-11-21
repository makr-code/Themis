#pragma once

#include <string>
#include <functional>
#include <chrono>
#include <vector>
#include <map>
#include "storage/base_entity.h"

namespace themis::exporters {

/// Export statistics collected during export
struct ExportStats {
    size_t total_entities = 0;
    size_t exported_entities = 0;
    size_t failed_entities = 0;
    size_t bytes_written = 0;
    std::chrono::milliseconds duration{0};
    std::vector<std::string> errors;
    
    std::string toJson() const;
};

/// Export options for configuring export behavior
struct ExportOptions {
    // Output file path
    std::string output_path;
    
    // Filtering
    std::vector<std::string> include_fields;  // If empty, export all fields
    std::vector<std::string> exclude_fields;
    std::string filter_expression;            // Optional filter (e.g., "category=active")
    
    // Format options
    bool pretty_print = false;
    bool compress = false;
    
    // Progress reporting
    std::function<void(const ExportStats&)> progress_callback;
    size_t progress_interval = 1000;  // Report every N entities
    
    // Error handling
    bool continue_on_error = true;
    size_t max_errors = 100;
};

/// Generic exporter interface
class IExporter {
public:
    virtual ~IExporter() = default;
    
    /// Export entities to the configured format
    /// @param entities Vector of entities to export
    /// @param options Export configuration
    /// @return Export statistics
    virtual ExportStats exportEntities(
        const std::vector<BaseEntity>& entities,
        const ExportOptions& options
    ) = 0;
    
    /// Get supported output formats
    virtual std::vector<std::string> getSupportedFormats() const = 0;
    
    /// Get exporter name
    virtual std::string getName() const = 0;
    
    /// Get exporter version
    virtual std::string getVersion() const = 0;
};

} // namespace themis::exporters
