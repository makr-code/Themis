#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <nlohmann/json.hpp>

namespace themis {
namespace content {

using json = nlohmann::json;

/**
 * @brief Content Type Categories
 * 
 * High-level classification of content types.
 * Each category has specific processing requirements.
 */
enum class ContentCategory {
    TEXT,        // Documents, code, JSON, XML, etc.
    IMAGE,       // Photos, diagrams, screenshots
    AUDIO,       // Music, speech, podcasts
    VIDEO,       // Movies, tutorials, recordings
    GEO,         // GIS data, maps, GPS tracks
    CAD,         // 3D models, technical drawings
    ARCHIVE,     // ZIP, TAR, etc.
    STRUCTURED,  // CSV, Parquet, Arrow tables
    BINARY,      // Generic binary data
    UNKNOWN
};

/**
 * @brief Content Type Definition
 * 
 * Describes a specific content type (e.g., "application/pdf", "image/jpeg").
 * Maps MIME types to processing strategies.
 */
struct ContentType {
    std::string mime_type;           // IANA MIME type (e.g., "text/plain")
    ContentCategory category;         // High-level category
    std::vector<std::string> extensions; // File extensions (e.g., [".txt", ".md"])
    bool supports_text_extraction;    // Can extract searchable text
    bool supports_embedding;          // Can generate embeddings
    bool supports_chunking;           // Should be chunked for RAG
    bool supports_metadata_extraction; // Can extract structured metadata
    bool binary_storage_required;     // Needs blob storage vs. can be stored as text
    
    // Feature flags for advanced processing
    struct Features {
        bool geospatial = false;      // Has lat/lon coordinates
        bool temporal = false;        // Has timestamps/duration
        bool hierarchical = false;    // Tree structure (e.g., CAD assemblies)
        bool versioned = false;       // Supports version history
        bool multimodal = false;      // Multiple data types (e.g., video = audio + images)
    } features;
    
    json toJson() const;
    static ContentType fromJson(const json& j);
};

/**
 * @brief Content Type Registry
 * 
 * Central registry for all supported content types.
 * Pre-configured with common types, extensible via plugins.
 */
class ContentTypeRegistry {
public:
    static ContentTypeRegistry& instance();
    
    /**
     * @brief Register a content type
     */
    void registerType(const ContentType& type);
    
    /**
     * @brief Lookup content type by MIME type
     */
    const ContentType* getByMimeType(const std::string& mime_type) const;
    
    /**
     * @brief Lookup content type by file extension
     */
    const ContentType* getByExtension(const std::string& extension) const;
    
    /**
     * @brief Detect content type from blob (magic bytes)
     */
    const ContentType* detectFromBlob(const std::string& blob) const;
    
    /**
     * @brief Get all types in a category
     */
    std::vector<const ContentType*> getByCategory(ContentCategory category) const;
    
    /**
     * @brief List all registered types
     */
    std::vector<const ContentType*> getAllTypes() const;

private:
    ContentTypeRegistry();
    void registerDefaultTypes();
    
    std::vector<ContentType> types_;
};

/**
 * @brief Initialize default content types
 * 
 * Pre-registers common types:
 * - TEXT: text/plain, text/markdown, text/html, application/json, text/x-python, etc.
 * - IMAGE: image/jpeg, image/png, image/svg+xml, image/tiff (GeoTIFF)
 * - AUDIO: audio/mpeg, audio/wav, audio/flac
 * - VIDEO: video/mp4, video/webm
 * - GEO: application/geo+json, application/vnd.geo+json, application/gpx+xml, image/tiff (GeoTIFF)
 * - CAD: model/step, model/iges, model/stl, application/dxf
 * - STRUCTURED: text/csv, application/vnd.apache.parquet, application/vnd.apache.arrow
 */
void initializeDefaultContentTypes();

} // namespace content
} // namespace themis
