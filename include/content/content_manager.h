#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include "content/content_type.h"
#include "content/content_processor.h"
#include "storage/base_entity.h"
#include "storage/rocksdb_wrapper.h"
#include "index/vector_index.h"
#include "index/graph_index.h"
#include "index/secondary_index.h"
#include "security/encryption.h"

namespace themis {
namespace content {

using json = nlohmann::json;

/**
 * @brief Content Metadata (Universal)
 * 
 * Unified metadata structure for all content types.
 * Primary Key: content:<uuid>
 */
struct ContentMeta {
    std::string id;                  // Content UUID (without "content:" prefix)
    std::string mime_type;           // MIME type
    ContentCategory category;        // Category
    std::string original_filename;   // Original filename
    int64_t size_bytes;              // Original size
    // Compression/Encryption flags for stored blob
    bool compressed = false;         // True if original blob stored compressed
    std::string compression_type;    // e.g., "zstd"
    bool encrypted = false;          // True if blob stored encrypted
    std::string encryption_type;     // e.g., "aes-256-gcm"
    int64_t created_at;              // Upload timestamp
    int64_t modified_at;             // Last modification
    std::string hash_sha256;         // Content hash (for deduplication)
    
    // Processing metadata
    bool text_extracted;             // Text extraction successful
    bool chunked;                    // Content has been chunked
    bool indexed;                    // Chunks indexed in VectorIndex
    int chunk_count;                 // Number of chunks created
    int embedding_dim;               // Embedding dimension (if applicable)
    
    // Type-specific metadata (from extraction)
    json extracted_metadata;         // EXIF, ID3, CAD properties, etc.
    
    // User metadata
    json user_metadata;              // Application-defined metadata
    std::vector<std::string> tags;   // User-defined tags
    
    // Relations
    std::string parent_id;           // Parent content (e.g., archive member)
    std::vector<std::string> child_ids; // Child content (e.g., CAD parts, archive files)
    
    json toJson() const;
    static ContentMeta fromJson(const json& j);
};

/**
 * @brief Chunk Metadata (Universal)
 * 
 * Represents a chunk from any content type.
 * Primary Key: chunk:<uuid>
 */
struct ChunkMeta {
    std::string id;                  // Chunk UUID (without "chunk:" prefix)
    std::string content_id;          // Parent content ID (FK to Content)
    int seq_num;                     // Sequence number within content (0-based)
    std::string chunk_type;          // "text", "image_region", "audio_segment", "table_row", etc.
    
    // Chunk data (type-dependent)
    std::string text;                // For text chunks
    json data;                       // For structured chunks (JSON, CSV row, etc.)
    std::string blob_ref;            // Reference to blob storage (for binary chunks)
    
    // Positional metadata
    int start_offset;                // Start position in original content
    int end_offset;                  // End position in original content
    
    // Embedding
    std::vector<float> embedding;    // Optional: Store embedding directly
    bool embedding_indexed;          // True if in VectorIndex
    
    int64_t created_at;              // Creation timestamp
    
    json toJson() const;
    static ChunkMeta fromJson(const json& j);
};

// Ingestion wurde entfernt. Stattdessen erwartet der Server bereits
// vorverarbeitete, strukturierte JSON-Objekte über /content/import.

/**
 * @brief Generic status type for ContentManager operations
 */
struct Status {
    bool ok = true;
    std::string message;
    static Status OK() { return {}; }
    static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
};

/**
 * @brief Content Manager
 * 
 * Universal content ingestion, storage, and retrieval system.
 * Handles all content types via pluggable processors.
 * 
 * Architecture:
 * 1. ContentTypeRegistry: MIME type → Category mapping
 * 2. ProcessorRegistry: Category → Processor mapping
 * 3. Storage: RocksDB (metadata + blobs), VectorIndex (embeddings), GraphIndex (relations)
 * 4. Unified API: ingestContent(), getContent(), searchContent(), etc.
 */
class ContentManager {
public:
    ContentManager(
        std::shared_ptr<RocksDBWrapper> storage,
        std::shared_ptr<VectorIndexManager> vector_index,
        std::shared_ptr<GraphIndexManager> graph_index,
        std::shared_ptr<SecondaryIndexManager> secondary_index,
        std::shared_ptr<FieldEncryption> field_encryption = nullptr
    );
    
    ~ContentManager() = default;

    /**
     * @brief Register a content processor
     * 
     * @param processor Processor instance (takes ownership)
     */
    void registerProcessor(std::unique_ptr<IContentProcessor> processor);

    /**
     * @brief Importiert bereits vorverarbeitete Inhalte (ohne Extraktion/Chunking/Embedding).
     * Erwartet ein strukturiertes JSON-Schema: { content: {...}, chunks: [...], edges?: [...] }.
     * Optional kann der Binärblob separat geliefert und gespeichert werden.
     * 
     * @param spec JSON-Objekt mit Content/Chunks/Edges
     * @param blob Optionaler Binärblob (wird unter content_blob:<id> gespeichert)
     * @return Status mit message; bei Erfolg ist message="ok"
     */
    // user_context: z.B. Benutzer-ID für kontextabhängige Verschlüsselung
    Status importContent(const json& spec, const std::optional<std::string>& blob = std::nullopt, const std::string& user_context = "");

    /**
     * @brief Get content metadata
     * 
     * @param content_id Content UUID (with or without "content:" prefix)
     * @return ContentMeta if found
     */
    std::optional<ContentMeta> getContentMeta(const std::string& content_id);

    /**
     * @brief Get content blob (original binary)
     * 
     * @param content_id Content UUID
     * @return Blob as string if found
     */
    std::optional<std::string> getContentBlob(const std::string& content_id, const std::string& user_context = "");

    /**
     * @brief Get all chunks for content (ordered by seq_num)
     * 
     * @param content_id Content UUID
     * @return Vector of ChunkMeta
     */
    std::vector<ChunkMeta> getContentChunks(const std::string& content_id);

    /**
     * @brief Get chunk metadata
     * 
     * @param chunk_id Chunk UUID
     * @return ChunkMeta if found
     */
    std::optional<ChunkMeta> getChunk(const std::string& chunk_id);

    /**
     * @brief Search content by semantic similarity
     * 
     * @param query_text Query text (will be embedded)
     * @param k Number of results
     * @param filters Optional filters (category, mime_type, tags)
     * @return Vector of (chunk_id, score) pairs
     */
    std::vector<std::pair<std::string, float>> searchContent(
        const std::string& query_text,
        int k,
        const json& filters = json::object()
    );

    /**
     * @brief Search with graph expansion (RAG-style)
     * 
     * Finds top-K chunks, then expands to neighbors (prev/next, siblings, parents).
     * 
     * @param query_text Query text
     * @param k Number of initial chunks
     * @param expansion_hops Graph expansion depth (1 = direct neighbors)
     * @param filters Optional filters
     * @return Vector of expanded chunk IDs with scores
     */
    std::vector<std::pair<std::string, float>> searchWithExpansion(
        const std::string& query_text,
        int k,
        int expansion_hops,
        const json& filters = json::object()
    );

    /**
     * @brief Delete content and all chunks (cascade)
     * 
     * @param content_id Content UUID
     * @return Status
     */
    Status deleteContent(const std::string& content_id);

    /**
     * @brief Get processor for a category
     */
    IContentProcessor* getProcessor(ContentCategory category);

    /**
     * @brief Get statistics
     */
    struct Stats {
        int total_content_items;
        int total_chunks;
        int total_embeddings;
        std::unordered_map<ContentCategory, int> items_by_category;
        int64_t total_storage_bytes;
    };
    Stats getStats();

private:
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<VectorIndexManager> vector_index_;
    std::shared_ptr<GraphIndexManager> graph_index_;
    std::shared_ptr<SecondaryIndexManager> secondary_index_;
    std::shared_ptr<FieldEncryption> field_encryption_;
    
    // Processor registry (Category → Processor)
    std::unordered_map<ContentCategory, std::unique_ptr<IContentProcessor>> processors_;

    // Helper methods
    std::string generateUuid();
    std::string normalizeId(const std::string& id, const std::string& prefix);
    std::string computeSHA256(const std::string& blob);
    std::optional<std::string> checkDuplicateByHash(const std::string& hash);
    
    void createChunkGraph(
        const std::vector<std::string>& chunk_ids,
        const std::string& content_id,
        const std::string& chunk_type
    );
    
    void createHierarchicalGraph(
        const std::string& parent_id,
        const std::vector<std::string>& child_ids,
        const std::string& edge_type
    );
};

} // namespace content
} // namespace themis
