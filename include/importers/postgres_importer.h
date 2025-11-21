#pragma once

#include "importers/importer_interface.h"
#include "plugins/plugin_interface.h"
#include <regex>
#include <atomic>

namespace themis {
namespace importers {

/**
 * @brief PostgreSQL pg_dump Importer
 * 
 * Imports data from PostgreSQL pg_dump files (SQL format).
 * Supports:
 * - DDL parsing (CREATE TABLE, CREATE SCHEMA)
 * - DML parsing (INSERT, COPY)
 * - Schema mapping to ThemisDB BaseEntity
 * - Type conversion
 * - Batch processing
 */
class PostgreSQLImporter : public IImporter {
public:
    PostgreSQLImporter();
    ~PostgreSQLImporter() override;
    
    // IImporter interface
    const char* getName() const override { return "PostgreSQL Importer"; }
    std::vector<std::string> getSupportedTypes() const override;
    bool initialize(const std::string& config) override;
    bool validateSource(const std::string& source_path, std::vector<std::string>& errors) override;
    ImportStats importData(
        const std::string& source_path,
        const ImportOptions& options,
        ProgressCallback progress_callback = nullptr
    ) override;
    void cancel() override;
    json getSourceSchema(const std::string& source_path) override;
    
private:
    struct TableSchema {
        std::string name;
        std::string schema;
        std::vector<std::string> columns;
        std::map<std::string, std::string> column_types;
        std::vector<std::string> primary_keys;
    };
    
    std::atomic<bool> cancelled_{false};
    std::map<std::string, TableSchema> schemas_;
    
    // Parsing methods
    bool parseDumpFile(const std::string& file_path, const ImportOptions& options, ImportStats& stats);
    bool parseCreateTable(const std::string& sql, TableSchema& schema);
    bool parseInsert(const std::string& sql, const ImportOptions& options, ImportStats& stats);
    bool parseCopy(std::ifstream& file, const std::string& table_name, const ImportOptions& options, ImportStats& stats);
    
    // Schema mapping
    std::string mapPostgreSQLTypeToThemis(const std::string& pg_type);
    bool shouldImportTable(const std::string& table_name, const ImportOptions& options);
    
    // Data conversion
    json convertRowToEntity(const TableSchema& schema, const std::vector<std::string>& values);
    
    // Progress reporting
    void reportProgress(ProgressCallback& callback, const std::string& stage, size_t current, size_t total);
};

/**
 * @brief PostgreSQL Importer Plugin
 * 
 * Wraps PostgreSQLImporter as a ThemisDB plugin
 */
class PostgreSQLImporterPlugin : public plugins::IThemisPlugin {
public:
    PostgreSQLImporterPlugin();
    ~PostgreSQLImporterPlugin() override = default;
    
    // IThemisPlugin interface
    const char* getName() const override { return "postgres_importer"; }
    const char* getVersion() const override { return "1.0.0"; }
    plugins::PluginType getType() const override { return plugins::PluginType::IMPORTER; }
    plugins::PluginCapabilities getCapabilities() const override;
    bool initialize(const char* config_json) override;
    void shutdown() override;
    void* getInstance() override { return importer_.get(); }
    
private:
    std::unique_ptr<PostgreSQLImporter> importer_;
};

} // namespace importers
} // namespace themis
