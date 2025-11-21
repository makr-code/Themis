#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace themis {
namespace importers {

using json = nlohmann::json;

/**
 * @brief Import Statistics
 */
struct ImportStats {
    size_t total_records = 0;
    size_t imported_records = 0;
    size_t failed_records = 0;
    size_t skipped_records = 0;
    
    size_t tables_processed = 0;
    size_t schemas_processed = 0;
    
    double elapsed_seconds = 0.0;
    
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    
    json toJson() const {
        return json{
            {"total_records", total_records},
            {"imported_records", imported_records},
            {"failed_records", failed_records},
            {"skipped_records", skipped_records},
            {"tables_processed", tables_processed},
            {"schemas_processed", schemas_processed},
            {"elapsed_seconds", elapsed_seconds},
            {"warnings", warnings},
            {"errors", errors}
        };
    }
};

/**
 * @brief Import Options
 */
struct ImportOptions {
    // General
    bool dry_run = false;                    // Don't actually import, just validate
    bool continue_on_error = true;           // Continue importing on row errors
    size_t batch_size = 1000;                // Records per batch
    
    // Schema mapping
    bool auto_create_schema = true;          // Auto-create missing entity types
    std::string default_namespace = "imported"; // Namespace for imported entities
    
    // Data handling
    bool preserve_ids = false;               // Try to preserve original IDs
    bool update_existing = false;            // Update if entity exists
    bool skip_duplicates = true;             // Skip duplicate records
    
    // Filtering
    std::vector<std::string> include_tables; // Only import these tables (empty = all)
    std::vector<std::string> exclude_tables; // Exclude these tables
    std::vector<std::string> include_schemas; // Only import these schemas
    
    // Transformations
    std::map<std::string, std::string> column_mappings; // Old column -> new attribute
    std::map<std::string, std::string> table_mappings;  // Old table -> new entity type
    
    json toJson() const {
        return json{
            {"dry_run", dry_run},
            {"continue_on_error", continue_on_error},
            {"batch_size", batch_size},
            {"auto_create_schema", auto_create_schema},
            {"default_namespace", default_namespace},
            {"preserve_ids", preserve_ids},
            {"update_existing", update_existing},
            {"skip_duplicates", skip_duplicates},
            {"include_tables", include_tables},
            {"exclude_tables", exclude_tables},
            {"include_schemas", include_schemas}
        };
    }
};

/**
 * @brief Progress Callback
 */
using ProgressCallback = std::function<void(const std::string& stage, size_t current, size_t total)>;

/**
 * @brief Base Importer Interface
 * 
 * All importers (PostgreSQL, MySQL, CSV, etc.) implement this interface.
 */
class IImporter {
public:
    virtual ~IImporter() = default;
    
    /**
     * @brief Get importer name
     */
    virtual const char* getName() const = 0;
    
    /**
     * @brief Get supported source types
     * @return List of supported types (e.g., "postgresql", "mysql", "csv")
     */
    virtual std::vector<std::string> getSupportedTypes() const = 0;
    
    /**
     * @brief Initialize importer with configuration
     * @param config Configuration JSON
     * @return true if initialized successfully
     */
    virtual bool initialize(const std::string& config) = 0;
    
    /**
     * @brief Validate source before import
     * @param source_path Path to source (file, directory, connection string)
     * @param errors Output: validation errors
     * @return true if source is valid
     */
    virtual bool validateSource(const std::string& source_path, std::vector<std::string>& errors) = 0;
    
    /**
     * @brief Import data from source
     * @param source_path Path to source
     * @param options Import options
     * @param progress_callback Optional progress callback
     * @return Import statistics
     */
    virtual ImportStats importData(
        const std::string& source_path,
        const ImportOptions& options,
        ProgressCallback progress_callback = nullptr
    ) = 0;
    
    /**
     * @brief Cancel ongoing import
     */
    virtual void cancel() = 0;
    
    /**
     * @brief Get schema information from source
     * @param source_path Path to source
     * @return Schema as JSON
     */
    virtual json getSourceSchema(const std::string& source_path) = 0;
};

} // namespace importers
} // namespace themis
