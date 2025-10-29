#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include "content/content_type.h"

namespace themis {
namespace content {

using json = nlohmann::json;

/**
 * @brief Extraction Result
 * 
 * Result of extracting structured data from content.
 */
struct ExtractionResult {
    bool ok;
    std::string text;              // Extracted plain text (for TEXT types)
    json metadata;                 // Structured metadata (EXIF, ID3, CAD properties, etc.)
    std::vector<float> embedding;  // Optional: Pre-computed embedding
    std::string error_message;
    
    // Type-specific extracted data
    struct GeoData {
        std::vector<std::pair<double, double>> coordinates; // lat/lon pairs
        std::string projection;    // EPSG code or WKT
        json properties;           // GeoJSON properties
    };
    std::optional<GeoData> geo_data;
    
    struct MediaData {
        int duration_seconds;      // For audio/video
        int width, height;         // For images/video
        std::string codec;
        int bitrate;
    };
    std::optional<MediaData> media_data;
    
    struct CADData {
        std::vector<std::string> part_ids;  // Assembly hierarchy
        json bom;                            // Bill of materials
        json dimensions;                     // Bounding box, volume, etc.
    };
    std::optional<CADData> cad_data;
};

/**
 * @brief Content Processor Interface
 * 
 * Abstract base class for content-type-specific processors.
 * Each processor handles extraction, chunking, and embedding for a category.
 */
class IContentProcessor {
public:
    virtual ~IContentProcessor() = default;
    
    /**
     * @brief Extract structured data from blob
     * 
     * @param blob Binary content
     * @param content_type Content type info
     * @return Extracted data (text, metadata, embeddings)
     */
    virtual ExtractionResult extract(
        const std::string& blob,
        const ContentType& content_type
    ) = 0;
    
    /**
     * @brief Chunk content for RAG/search
     * 
     * @param extraction_result Previously extracted data
     * @param chunk_size Target chunk size (tokens or other unit)
     * @param overlap Overlap between chunks
     * @return Vector of chunks with metadata
     */
    virtual std::vector<json> chunk(
        const ExtractionResult& extraction_result,
        int chunk_size,
        int overlap
    ) = 0;
    
    /**
     * @brief Generate embedding for a chunk
     * 
     * @param chunk_data Chunk data (text or other representation)
     * @return Embedding vector
     */
    virtual std::vector<float> generateEmbedding(const std::string& chunk_data) = 0;
    
    /**
     * @brief Get processor name
     */
    virtual std::string getName() const = 0;
    
    /**
     * @brief Get supported categories
     */
    virtual std::vector<ContentCategory> getSupportedCategories() const = 0;
};

/**
 * @brief Text Content Processor
 * 
 * Handles text documents, code, JSON, XML, Markdown, etc.
 */
class TextProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "TextProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::TEXT};
    }

private:
    std::string normalizeText(const std::string& text);
    int countTokens(const std::string& text); // Simple whitespace-based tokenizer
    std::vector<std::string> splitIntoSentences(const std::string& text);
};

/**
 * @brief Image Content Processor
 * 
 * Handles photos, diagrams, screenshots.
 * Extracts EXIF metadata, generates image embeddings (e.g., CLIP).
 */
class ImageProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "ImageProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::IMAGE};
    }

private:
    json extractEXIF(const std::string& blob);
    std::pair<int, int> getImageDimensions(const std::string& blob);
};

/**
 * @brief Geo Content Processor
 * 
 * Handles GeoJSON, GPX, Shapefiles, GeoTIFF.
 * Extracts coordinates, creates spatial indices.
 */
class GeoProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "GeoProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::GEO};
    }

private:
    ExtractionResult::GeoData parseGeoJSON(const std::string& blob);
    ExtractionResult::GeoData parseGPX(const std::string& blob);
};

/**
 * @brief CAD Content Processor
 * 
 * Handles STEP, IGES, STL, DXF.
 * Extracts geometry, assemblies, bill of materials.
 */
class CADProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "CADProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::CAD};
    }

private:
    json parseSTEP(const std::string& blob);
    json extractAssemblyHierarchy(const json& step_data);
};

/**
 * @brief Audio Content Processor
 * 
 * Handles MP3, WAV, FLAC.
 * Extracts ID3 tags, transcribes speech (optional), generates audio embeddings.
 */
class AudioProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "AudioProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::AUDIO};
    }

private:
    json extractID3Tags(const std::string& blob);
    int getDurationSeconds(const std::string& blob);
};

/**
 * @brief Structured Data Processor
 * 
 * Handles CSV, Parquet, Arrow tables.
 * Creates row-level chunks, column embeddings.
 */
class StructuredProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "StructuredProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::STRUCTURED};
    }

private:
    std::vector<std::vector<std::string>> parseCSV(const std::string& blob);
    json extractSchema(const std::vector<std::vector<std::string>>& rows);
};

/**
 * @brief Generic Binary Processor (Fallback)
 * 
 * Handles unknown binary types.
 * Stores blob, extracts minimal metadata (size, hash).
 */
class BinaryProcessor : public IContentProcessor {
public:
    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "BinaryProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override {
        return {ContentCategory::BINARY, ContentCategory::ARCHIVE, ContentCategory::UNKNOWN};
    }

private:
    std::string computeHash(const std::string& blob);
};

} // namespace content
} // namespace themis
