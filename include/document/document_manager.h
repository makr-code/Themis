#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include "storage/base_entity.h"
#include "storage/rocksdb_wrapper.h"
#include "index/vector_index.h"
#include "index/graph_index.h"

namespace themis {
namespace document {

using json = nlohmann::json;

/**
 * @brief Document Metadata
 * 
 * Stores metadata about uploaded documents.
 * Primary Key: doc:<uuid>
 */
struct DocumentMeta {
    std::string id;              // Document UUID (without "doc:" prefix)
    std::string title;           // Document title/filename
    std::string source;          // Original filename or URL
    std::string mime_type;       // MIME type (text/plain, application/pdf, etc.)
    int64_t size_bytes;          // Original file size
    int64_t created_at;          // Unix timestamp (seconds)
    int embedding_dim;           // Embedding dimension (e.g., 768 for MPNet)
    int chunk_count;             // Number of chunks created
    std::string chunking_strategy; // "fixed_size_512_overlap_50"
    json metadata;               // Additional user metadata (JSON object)
    
    // Serialization
    json toJson() const;
    static DocumentMeta fromJson(const json& j);
};

/**
 * @brief Chunk Metadata
 * 
 * Represents a text chunk from a document with embedding.
 * Primary Key: chunk:<uuid>
 */
struct ChunkMeta {
    std::string id;              // Chunk UUID (without "chunk:" prefix)
    std::string doc_id;          // Parent document ID (FK to Document)
    int seq_num;                 // Sequence number within document (0-based)
    std::string text;            // Chunk text content
    int start_offset;            // Start position in original document (char index)
    int end_offset;              // End position in original document (char index)
    std::vector<float> embedding; // Embedding vector (optional, can be in VectorIndex only)
    int64_t created_at;          // Unix timestamp (seconds)
    
    // Serialization
    json toJson() const;
    static ChunkMeta fromJson(const json& j);
};

/**
 * @brief Chunking Configuration
 */
struct ChunkingConfig {
    int chunk_size_tokens = 512;   // Target chunk size in tokens
    int overlap_tokens = 50;       // Overlap between chunks
    bool preserve_sentences = true; // Try to break at sentence boundaries
    
    std::string toStrategyString() const {
        return "fixed_size_" + std::to_string(chunk_size_tokens) + 
               "_overlap_" + std::to_string(overlap_tokens);
    }
};

/**
 * @brief Result of document upload and processing
 */
struct UploadResult {
    bool ok;
    std::string doc_id;
    int chunks_created;
    std::string message;
};

/**
 * @brief Document Manager
 * 
 * Manages document upload, text extraction, chunking, embedding, and graph construction.
 * Integrates with VectorIndexManager and GraphIndexManager.
 */
class DocumentManager {
public:
    DocumentManager(
        std::shared_ptr<RocksDBWrapper> storage,
        std::shared_ptr<VectorIndexManager> vector_index,
        std::shared_ptr<GraphIndexManager> graph_index
    );
    
    ~DocumentManager() = default;

    /**
     * @brief Upload and process a document
     * 
     * Steps:
     * 1. Generate document UUID
     * 2. Store binary blob in RocksDB (if store_blob=true)
     * 3. Extract text from blob (based on mime_type)
     * 4. Chunk text with overlap
     * 5. Generate embeddings for each chunk (external API or mock)
     * 6. Insert chunks into VectorIndex
     * 7. Create graph edges (parent, next/prev)
     * 8. Store document metadata
     * 
     * @param blob Binary content (can be empty if text is provided directly)
     * @param mime_type MIME type (e.g., "text/plain", "application/pdf")
     * @param filename Original filename
     * @param text Optional: Pre-extracted text (skips step 3)
     * @param user_metadata Optional: Additional metadata (JSON object)
     * @param store_blob If true, stores blob in RocksDB; if false, only processes text
     * @return UploadResult with doc_id and status
     */
    UploadResult uploadDocument(
        const std::string& blob,
        const std::string& mime_type,
        const std::string& filename,
        const std::optional<std::string>& text = std::nullopt,
        const json& user_metadata = json::object(),
        bool store_blob = true
    );

    /**
     * @brief Get document metadata
     * 
     * @param doc_id Document UUID (with or without "doc:" prefix)
     * @return DocumentMeta if found, std::nullopt otherwise
     */
    std::optional<DocumentMeta> getDocument(const std::string& doc_id);

    /**
     * @brief Get document blob (binary content)
     * 
     * @param doc_id Document UUID
     * @return Blob as string if found, std::nullopt otherwise
     */
    std::optional<std::string> getDocumentBlob(const std::string& doc_id);

    /**
     * @brief Get all chunks for a document (ordered by seq_num)
     * 
     * @param doc_id Document UUID
     * @return Vector of ChunkMeta
     */
    std::vector<ChunkMeta> getDocumentChunks(const std::string& doc_id);

    /**
     * @brief Get chunk metadata
     * 
     * @param chunk_id Chunk UUID (with or without "chunk:" prefix)
     * @return ChunkMeta if found, std::nullopt otherwise
     */
    std::optional<ChunkMeta> getChunk(const std::string& chunk_id);

    /**
     * @brief Delete document and all chunks (cascade)
     * 
     * @param doc_id Document UUID
     * @return Status with ok=true if deleted
     */
    Status deleteDocument(const std::string& doc_id);

    /**
     * @brief Set chunking configuration
     */
    void setChunkingConfig(const ChunkingConfig& config) {
        chunking_config_ = config;
    }

    /**
     * @brief Get current chunking configuration
     */
    ChunkingConfig getChunkingConfig() const {
        return chunking_config_;
    }

private:
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<VectorIndexManager> vector_index_;
    std::shared_ptr<GraphIndexManager> graph_index_;
    
    ChunkingConfig chunking_config_;

    // Helper methods
    std::string generateUuid();
    std::string normalizeId(const std::string& id, const std::string& prefix);
    std::string extractText(const std::string& blob, const std::string& mime_type);
    std::vector<ChunkMeta> chunkText(
        const std::string& text,
        const std::string& doc_id,
        int embedding_dim
    );
    std::vector<float> generateEmbedding(const std::string& text);
    void createChunkGraph(const std::vector<std::string>& chunk_ids, const std::string& doc_id);
};

} // namespace document
} // namespace themis
