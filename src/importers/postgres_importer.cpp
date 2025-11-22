#include "importers/postgres_importer.h"
#include "utils/logger.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace themis {
namespace importers {

// ============================================================================
// PostgreSQLImporter Implementation
// ============================================================================

PostgreSQLImporter::PostgreSQLImporter() {
}

PostgreSQLImporter::~PostgreSQLImporter() {
    cancel();
}

std::vector<std::string> PostgreSQLImporter::getSupportedTypes() const {
    return {"postgresql", "postgres", "pg_dump"};
}

bool PostgreSQLImporter::initialize(const std::string& config) {
    cancelled_ = false;
    schemas_.clear();
    
    THEMIS_INFO("PostgreSQL Importer initialized");
    return true;
}

bool PostgreSQLImporter::validateSource(const std::string& source_path, std::vector<std::string>& errors) {
    std::ifstream file(source_path);
    if (!file) {
        errors.push_back("Cannot open file: " + source_path);
        return false;
    }
    
    // Check if it looks like a PostgreSQL dump
    std::string line;
    bool found_pg_dump = false;
    int lines_checked = 0;
    
    while (std::getline(file, line) && lines_checked < 100) {
        if (line.find("PostgreSQL database dump") != std::string::npos ||
            line.find("pg_dump") != std::string::npos ||
            line.find("-- Dumped from database version") != std::string::npos) {
            found_pg_dump = true;
            break;
        }
        lines_checked++;
    }
    
    if (!found_pg_dump) {
        errors.push_back("File does not appear to be a PostgreSQL dump");
        return false;
    }
    
    THEMIS_INFO("Source validation successful: {}", source_path);
    return true;
}

ImportStats PostgreSQLImporter::importData(
    const std::string& source_path,
    const ImportOptions& options,
    ProgressCallback progress_callback
) {
    ImportStats stats;
    auto start_time = std::chrono::steady_clock::now();
    
    THEMIS_INFO("Starting PostgreSQL import from: {}", source_path);
    THEMIS_INFO("Options: {}", options.toJson().dump());
    
    if (options.dry_run) {
        THEMIS_INFO("DRY RUN MODE - No data will be imported");
    }
    
    // Parse dump file
    if (!parseDumpFile(source_path, options, stats)) {
        stats.errors.push_back("Failed to parse dump file");
    }
    
    auto end_time = std::chrono::steady_clock::now();
    stats.elapsed_seconds = std::chrono::duration<double>(end_time - start_time).count();
    
    THEMIS_INFO("Import completed: {} records imported, {} failed, {} skipped in {:.2f}s",
        stats.imported_records, stats.failed_records, stats.skipped_records, stats.elapsed_seconds);
    
    return stats;
}

void PostgreSQLImporter::cancel() {
    cancelled_ = true;
    THEMIS_INFO("Import cancelled");
}

json PostgreSQLImporter::getSourceSchema(const std::string& source_path) {
    schemas_.clear();
    
    std::ifstream file(source_path);
    if (!file) {
        return json::array();
    }
    
    std::string line;
    std::string current_sql;
    
    while (std::getline(file, line)) {
        // Skip comments
        if (line.empty() || line[0] == '-') continue;
        
        current_sql += line + " ";
        
        // Complete statement?
        if (line.find(';') != std::string::npos) {
            if (current_sql.find("CREATE TABLE") != std::string::npos) {
                TableSchema schema;
                if (parseCreateTable(current_sql, schema)) {
                    schemas_[schema.name] = schema;
                }
            }
            current_sql.clear();
        }
    }
    
    // Convert to JSON
    json result = json::array();
    for (const auto& [name, schema] : schemas_) {
        json table_json = {
            {"name", schema.name},
            {"schema", schema.schema},
            {"columns", schema.columns},
            {"column_types", schema.column_types},
            {"primary_keys", schema.primary_keys}
        };
        result.push_back(table_json);
    }
    
    return result;
}

// ============================================================================
// Private Methods
// ============================================================================

bool PostgreSQLImporter::parseDumpFile(const std::string& file_path, const ImportOptions& options, ImportStats& stats) {
    std::ifstream file(file_path);
    if (!file) {
        stats.errors.push_back("Cannot open file: " + file_path);
        return false;
    }
    
    std::string line;
    std::string current_sql;
    size_t line_number = 0;
    
    while (std::getline(file, line) && !cancelled_) {
        line_number++;
        
        // Skip comments
        if (line.empty() || (line.size() >= 2 && line[0] == '-' && line[1] == '-')) {
            continue;
        }
        
        current_sql += line + " ";
        
        // Complete statement?
        if (line.find(';') != std::string::npos) {
            // Parse different statement types
            if (current_sql.find("CREATE TABLE") != std::string::npos ||
                current_sql.find("CREATE SCHEMA") != std::string::npos) {
                TableSchema schema;
                if (parseCreateTable(current_sql, schema)) {
                    if (shouldImportTable(schema.name, options)) {
                        schemas_[schema.name] = schema;
                        stats.tables_processed++;
                        THEMIS_DEBUG("Parsed table schema: {}", schema.name);
                    }
                }
            }
            else if (current_sql.find("INSERT INTO") != std::string::npos) {
                if (!options.dry_run) {
                    parseInsert(current_sql, options, stats);
                }
                stats.total_records++;
            }
            else if (current_sql.find("COPY ") != std::string::npos) {
                // COPY statement - read data until \.
                std::string table_name;
                std::regex copy_regex(R"(COPY\s+(\w+)\s+)");
                std::smatch match;
                if (std::regex_search(current_sql, match, copy_regex)) {
                    table_name = match[1].str();
                    if (!options.dry_run) {
                        parseCopy(file, table_name, options, stats);
                    }
                }
            }
            
            current_sql.clear();
        }
    }
    
    return !cancelled_;
}

bool PostgreSQLImporter::parseCreateTable(const std::string& sql, TableSchema& schema) {
    // Simple regex-based parsing (production would use a proper SQL parser)
    std::regex table_regex(R"(CREATE TABLE\s+(?:(\w+)\.)?(\w+)\s*\()");
    std::smatch match;
    
    if (std::regex_search(sql, match, table_regex)) {
        if (match.size() > 2) {
            schema.schema = match[1].str();
            schema.name = match[2].str();
        } else {
            schema.name = match[1].str();
        }
        
        // Extract columns (simplified)
        size_t start = sql.find('(');
        size_t end = sql.find_last_of(')');
        if (start != std::string::npos && end != std::string::npos) {
            std::string columns_str = sql.substr(start + 1, end - start - 1);
            
            // Split by comma (simplified - doesn't handle nested parentheses)
            std::stringstream ss(columns_str);
            std::string column_def;
            
            while (std::getline(ss, column_def, ',')) {
                // Trim
                column_def.erase(0, column_def.find_first_not_of(" \t\n\r"));
                column_def.erase(column_def.find_last_not_of(" \t\n\r") + 1);
                
                if (column_def.empty() || column_def.find("CONSTRAINT") != std::string::npos) {
                    continue;
                }
                
                // Extract column name and type
                std::istringstream col_ss(column_def);
                std::string col_name, col_type;
                col_ss >> col_name >> col_type;
                
                if (!col_name.empty() && !col_type.empty()) {
                    schema.columns.push_back(col_name);
                    schema.column_types[col_name] = col_type;
                }
            }
        }
        
        return !schema.name.empty();
    }
    
    return false;
}

bool PostgreSQLImporter::parseInsert(const std::string& sql, const ImportOptions& options, ImportStats& stats) {
    // Simplified INSERT parsing
    // Real implementation would use a proper SQL parser
    
    // Extract table name
    std::regex insert_regex(R"(INSERT INTO\s+(\w+))");
    std::smatch match;
    
    if (std::regex_search(sql, match, insert_regex)) {
        std::string table_name = match[1].str();
        
        if (!shouldImportTable(table_name, options)) {
            stats.skipped_records++;
            return true;
        }
        
        // TODO: Parse VALUES and convert to ThemisDB entity
        stats.imported_records++;
        return true;
    }
    
    stats.failed_records++;
    return false;
}

bool PostgreSQLImporter::parseCopy(std::ifstream& file, const std::string& table_name, const ImportOptions& options, ImportStats& stats) {
    if (!shouldImportTable(table_name, options)) {
        // Skip until end marker
        std::string line;
        while (std::getline(file, line)) {
            if (line == "\\." || line.find("\\.") == 0) {
                break;
            }
            stats.skipped_records++;
        }
        return true;
    }
    
    std::string line;
    while (std::getline(file, line) && !cancelled_) {
        if (line == "\\." || line.find("\\.") == 0) {
            break;  // End of COPY data
        }
        
        // Parse tab-separated values
        // TODO: Convert to ThemisDB entity
        stats.total_records++;
        stats.imported_records++;
    }
    
    return true;
}

std::string PostgreSQLImporter::mapPostgreSQLTypeToThemis(const std::string& pg_type) {
    std::string lower_type = pg_type;
    std::transform(lower_type.begin(), lower_type.end(), lower_type.begin(), ::tolower);
    
    // Map PostgreSQL types to ThemisDB types
    if (lower_type.find("int") != std::string::npos) return "integer";
    if (lower_type.find("serial") != std::string::npos) return "integer";
    if (lower_type.find("bigint") != std::string::npos) return "long";
    if (lower_type.find("smallint") != std::string::npos) return "integer";
    if (lower_type.find("real") != std::string::npos) return "double";
    if (lower_type.find("double") != std::string::npos) return "double";
    if (lower_type.find("numeric") != std::string::npos) return "double";
    if (lower_type.find("decimal") != std::string::npos) return "double";
    if (lower_type.find("bool") != std::string::npos) return "boolean";
    if (lower_type.find("char") != std::string::npos) return "string";
    if (lower_type.find("text") != std::string::npos) return "string";
    if (lower_type.find("varchar") != std::string::npos) return "string";
    if (lower_type.find("timestamp") != std::string::npos) return "datetime";
    if (lower_type.find("date") != std::string::npos) return "date";
    if (lower_type.find("time") != std::string::npos) return "time";
    if (lower_type.find("json") != std::string::npos) return "json";
    if (lower_type.find("uuid") != std::string::npos) return "string";
    if (lower_type.find("bytea") != std::string::npos) return "binary";
    
    return "string";  // Default
}

bool PostgreSQLImporter::shouldImportTable(const std::string& table_name, const ImportOptions& options) {
    // Check exclude list
    if (std::find(options.exclude_tables.begin(), options.exclude_tables.end(), table_name) != options.exclude_tables.end()) {
        return false;
    }
    
    // Check include list (if specified)
    if (!options.include_tables.empty()) {
        return std::find(options.include_tables.begin(), options.include_tables.end(), table_name) != options.include_tables.end();
    }
    
    return true;
}

json PostgreSQLImporter::convertRowToEntity(const TableSchema& schema, const std::vector<std::string>& values) {
    json entity;
    entity["_type"] = schema.name;
    
    for (size_t i = 0; i < values.size() && i < schema.columns.size(); i++) {
        entity[schema.columns[i]] = values[i];
    }
    
    return entity;
}

void PostgreSQLImporter::reportProgress(ProgressCallback& callback, const std::string& stage, size_t current, size_t total) {
    if (callback) {
        callback(stage, current, total);
    }
}

// ============================================================================
// PostgreSQLImporterPlugin Implementation
// ============================================================================

PostgreSQLImporterPlugin::PostgreSQLImporterPlugin() 
    : importer_(std::make_unique<PostgreSQLImporter>()) {
}

plugins::PluginCapabilities PostgreSQLImporterPlugin::getCapabilities() const {
    plugins::PluginCapabilities caps;
    caps.supports_streaming = true;
    caps.supports_batching = true;
    caps.thread_safe = false;  // Not thread-safe (uses instance state)
    return caps;
}

bool PostgreSQLImporterPlugin::initialize(const char* config_json) {
    if (!importer_) {
        return false;
    }
    return importer_->initialize(config_json ? config_json : "{}");
}

void PostgreSQLImporterPlugin::shutdown() {
    if (importer_) {
        importer_->cancel();
    }
}

} // namespace importers
} // namespace themis

// ============================================================================
// Plugin Entry Points
// ============================================================================

extern "C" {
    THEMIS_PLUGIN_EXPORT themis::plugins::IThemisPlugin* createPlugin() {
        return new themis::importers::PostgreSQLImporterPlugin();
    }
    
    THEMIS_PLUGIN_EXPORT void destroyPlugin(themis::plugins::IThemisPlugin* plugin) {
        delete plugin;
    }
}
