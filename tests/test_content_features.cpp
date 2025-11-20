// ============================================================================
// ThemisDB - Content Feature Integration Tests
// Tests for: Search API, Filesystem API, Content Assembly
// ============================================================================

#include <gtest/gtest.h>
#include "content/content_manager.h"
#include "storage/rocksdb_wrapper.h"
#include "index/vector_index_manager.h"
#include "index/secondary_index_manager.h"
#include <filesystem>

using namespace themis;

class ContentFeaturesTest : public ::testing::Test {
protected:
    std::shared_ptr<RocksDBWrapper> storage;
    std::shared_ptr<VectorIndexManager> vector_mgr;
    std::shared_ptr<SecondaryIndexManager> secondary_mgr;
    std::shared_ptr<ContentManager> content_mgr;
    
    std::string test_db_path = "./test_content_features_db";

    void SetUp() override {
        // Clean up test database
        if (std::filesystem::exists(test_db_path)) {
            std::filesystem::remove_all(test_db_path);
        }

        // Initialize storage
        storage = std::make_shared<RocksDBWrapper>();
        auto status = storage->open(test_db_path);
        ASSERT_TRUE(status.ok()) << "Failed to open storage: " << status.message();

        // Initialize index managers
        vector_mgr = std::make_shared<VectorIndexManager>(storage);
        secondary_mgr = std::make_shared<SecondaryIndexManager>(storage);

        // Initialize content manager
        content_mgr = std::make_shared<ContentManager>(storage, vector_mgr, secondary_mgr);
    }

    void TearDown() override {
        content_mgr.reset();
        secondary_mgr.reset();
        vector_mgr.reset();
        storage.reset();

        // Clean up test database
        if (std::filesystem::exists(test_db_path)) {
            std::filesystem::remove_all(test_db_path);
        }
    }

    // Helper: Import content with multiple chunks
    std::string importTestContent(const std::string& filename, 
                                   const std::string& mime_type,
                                   const std::vector<std::string>& chunk_texts) {
        ContentManager::ContentMeta meta;
        meta.filename = filename;
        meta.mime_type = mime_type;
        meta.total_size_bytes = 0;
        meta.chunk_count = chunk_texts.size();

        auto result = content_mgr->importContent(meta, chunk_texts);
        EXPECT_TRUE(result.has_value()) << "Failed to import content";
        
        return result.has_value() ? result.value() : "";
    }
};

// ============================================================================
// Content Search API Tests
// ============================================================================

TEST_F(ContentFeaturesTest, SearchHybrid_VectorOnly) {
    // Import test content
    std::vector<std::string> chunks = {
        "Machine learning algorithms analyze data patterns.",
        "Neural networks are inspired by biological brains.",
        "Deep learning uses multiple layers for feature extraction."
    };
    
    std::string content_id = importTestContent("ml_guide.txt", "text/plain", chunks);
    ASSERT_FALSE(content_id.empty());

    // Perform hybrid search (vector-only, weight=1.0)
    auto results = content_mgr->searchContentHybrid(
        "artificial intelligence",  // query
        5,                          // k
        {},                         // no filters
        1.0,                        // vector_weight
        0.0,                        // fulltext_weight (disabled)
        60                          // rrf_k
    );

    ASSERT_FALSE(results.empty()) << "Search should return results";
    
    // Verify chunk IDs are valid
    for (const auto& [chunk_id, score] : results) {
        EXPECT_FALSE(chunk_id.empty());
        EXPECT_GT(score, 0.0);
    }
}

TEST_F(ContentFeaturesTest, SearchHybrid_FulltextOnly) {
    // Import content
    std::vector<std::string> chunks = {
        "Python is a high-level programming language.",
        "JavaScript runs in web browsers and Node.js.",
        "C++ offers performance and low-level control."
    };
    
    std::string content_id = importTestContent("programming.txt", "text/plain", chunks);
    ASSERT_FALSE(content_id.empty());

    // Fulltext search only (vector disabled)
    auto results = content_mgr->searchContentHybrid(
        "programming language",
        5,
        {},
        0.0,    // vector_weight (disabled)
        1.0,    // fulltext_weight
        60
    );

    ASSERT_FALSE(results.empty());
    
    // First result should have highest relevance
    EXPECT_GT(results[0].second, 0.0);
}

TEST_F(ContentFeaturesTest, SearchHybrid_RRF_Fusion) {
    // Import diverse content
    std::vector<std::string> chunks = {
        "Database indexing improves query performance significantly.",
        "Vector similarity search enables semantic retrieval.",
        "Fulltext search uses inverted indexes for fast lookups."
    };
    
    std::string content_id = importTestContent("database_optimization.txt", "text/plain", chunks);
    ASSERT_FALSE(content_id.empty());

    // Hybrid search with equal weights (RRF fusion)
    auto results = content_mgr->searchContentHybrid(
        "search indexing",
        10,
        {},
        0.5,    // vector_weight
        0.5,    // fulltext_weight
        60      // rrf_k
    );

    ASSERT_FALSE(results.empty());
    
    // Verify scores are fused (should be different from pure vector or fulltext)
    EXPECT_GT(results.size(), 0);
    for (size_t i = 0; i < results.size() - 1; ++i) {
        // Scores should be descending
        EXPECT_GE(results[i].second, results[i+1].second);
    }
}

// ============================================================================
// Filesystem Interface Tests
// ============================================================================

TEST_F(ContentFeaturesTest, ResolvePath_Basic) {
    // Import content and register path
    std::vector<std::string> chunks = {"Document content here."};
    std::string content_id = importTestContent("report.pdf", "application/pdf", chunks);
    ASSERT_FALSE(content_id.empty());

    // Register virtual path
    auto status = content_mgr->registerPath(content_id, "/documents/reports/Q4_2024.pdf");
    EXPECT_EQ(status, ContentManager::Status::Ok);

    // Resolve path
    auto resolved = content_mgr->resolvePath("/documents/reports/Q4_2024.pdf");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved.value().id, content_id);
    EXPECT_EQ(resolved.value().virtual_path, "/documents/reports/Q4_2024.pdf");
}

TEST_F(ContentFeaturesTest, CreateDirectory_Recursive) {
    // Create nested directory structure
    auto status = content_mgr->createDirectory("/data/geo/layers", true);
    EXPECT_EQ(status, ContentManager::Status::Ok);

    // Verify directory exists
    auto resolved = content_mgr->resolvePath("/data/geo/layers");
    ASSERT_TRUE(resolved.has_value());
    EXPECT_TRUE(resolved.value().is_directory);
}

TEST_F(ContentFeaturesTest, ListDirectory_Contents) {
    // Create directory and add files
    content_mgr->createDirectory("/projects", false);
    
    std::string file1_id = importTestContent("readme.md", "text/markdown", {"# Project"});
    std::string file2_id = importTestContent("config.json", "application/json", {"{}"});
    
    content_mgr->registerPath(file1_id, "/projects/readme.md");
    content_mgr->registerPath(file2_id, "/projects/config.json");

    // List directory contents
    auto contents = content_mgr->listDirectory("/projects");
    ASSERT_TRUE(contents.has_value());
    EXPECT_GE(contents.value().size(), 2);

    // Verify file metadata
    bool found_readme = false;
    bool found_config = false;
    
    for (const auto& item : contents.value()) {
        if (item.filename == "readme.md") found_readme = true;
        if (item.filename == "config.json") found_config = true;
    }
    
    EXPECT_TRUE(found_readme);
    EXPECT_TRUE(found_config);
}

// ============================================================================
// Content Assembly & Navigation Tests
// ============================================================================

TEST_F(ContentFeaturesTest, AssembleContent_WithoutText) {
    // Import multi-chunk content
    std::vector<std::string> chunks = {
        "Chapter 1: Introduction to Databases",
        "Chapter 2: Relational Model",
        "Chapter 3: Query Optimization",
        "Chapter 4: Transactions and Concurrency"
    };
    
    std::string content_id = importTestContent("database_book.txt", "text/plain", chunks);
    ASSERT_FALSE(content_id.empty());

    // Assemble without full text
    auto assembly = content_mgr->assembleContent(content_id, false);
    ASSERT_TRUE(assembly.has_value());

    EXPECT_EQ(assembly.value().metadata.id, content_id);
    EXPECT_EQ(assembly.value().chunks.size(), 4);
    EXPECT_FALSE(assembly.value().assembled_text.has_value());  // No text loaded
    EXPECT_GT(assembly.value().total_size_bytes, 0);
}

TEST_F(ContentFeaturesTest, AssembleContent_WithText) {
    // Import content
    std::vector<std::string> chunks = {
        "First paragraph of the article.",
        "Second paragraph continues the topic.",
        "Conclusion summarizes the findings."
    };
    
    std::string content_id = importTestContent("article.txt", "text/plain", chunks);
    ASSERT_FALSE(content_id.empty());

    // Assemble with full text
    auto assembly = content_mgr->assembleContent(content_id, true);
    ASSERT_TRUE(assembly.has_value());

    EXPECT_TRUE(assembly.value().assembled_text.has_value());
    
    std::string full_text = assembly.value().assembled_text.value();
    EXPECT_TRUE(full_text.find("First paragraph") != std::string::npos);
    EXPECT_TRUE(full_text.find("Second paragraph") != std::string::npos);
    EXPECT_TRUE(full_text.find("Conclusion") != std::string::npos);
}

TEST_F(ContentFeaturesTest, ChunkNavigation_NextPrevious) {
    // Import sequential chunks
    std::vector<std::string> chunks = {
        "Page 1 content",
        "Page 2 content",
        "Page 3 content",
        "Page 4 content"
    };
    
    std::string content_id = importTestContent("paginated_doc.txt", "text/plain", chunks);
    ASSERT_FALSE(content_id.empty());

    // Get all chunks
    auto all_chunks = content_mgr->getContentChunks(content_id);
    ASSERT_EQ(all_chunks.size(), 4);

    // Navigate forward (page 2 -> page 3)
    std::string chunk2_id = all_chunks[1].id;
    auto next_chunk = content_mgr->getNextChunk(chunk2_id);
    ASSERT_TRUE(next_chunk.has_value());
    EXPECT_EQ(next_chunk.value().seq_num, 2);  // seq_num 0-indexed
    EXPECT_TRUE(next_chunk.value().text.find("Page 3") != std::string::npos);

    // Navigate backward (page 3 -> page 2)
    std::string chunk3_id = next_chunk.value().id;
    auto prev_chunk = content_mgr->getPreviousChunk(chunk3_id);
    ASSERT_TRUE(prev_chunk.has_value());
    EXPECT_EQ(prev_chunk.value().seq_num, 1);
    EXPECT_TRUE(prev_chunk.value().text.find("Page 2") != std::string::npos);
}

TEST_F(ContentFeaturesTest, GetChunkRange_Pagination) {
    // Import 10 chunks
    std::vector<std::string> chunks;
    for (int i = 1; i <= 10; ++i) {
        chunks.push_back("Chunk " + std::to_string(i) + " data");
    }
    
    std::string content_id = importTestContent("large_file.bin", "application/octet-stream", chunks);
    ASSERT_FALSE(content_id.empty());

    // Get chunks 3-6 (4 chunks, start_seq=2, count=4)
    auto range = content_mgr->getChunkRange(content_id, 2, 4);
    ASSERT_EQ(range.size(), 4);

    EXPECT_EQ(range[0].seq_num, 2);  // Chunk 3
    EXPECT_EQ(range[1].seq_num, 3);  // Chunk 4
    EXPECT_EQ(range[2].seq_num, 4);  // Chunk 5
    EXPECT_EQ(range[3].seq_num, 5);  // Chunk 6

    EXPECT_TRUE(range[0].text.find("Chunk 3") != std::string::npos);
    EXPECT_TRUE(range[3].text.find("Chunk 6") != std::string::npos);
}

// ============================================================================
// Integration Tests - Combined Features
// ============================================================================

TEST_F(ContentFeaturesTest, Integration_SearchAndAssemble) {
    // Import documents
    std::vector<std::string> doc1_chunks = {
        "Climate change affects global temperatures.",
        "Rising sea levels threaten coastal cities.",
        "Renewable energy reduces carbon emissions."
    };
    
    std::vector<std::string> doc2_chunks = {
        "Solar panels convert sunlight to electricity.",
        "Wind turbines generate clean energy.",
        "Hydroelectric dams use water flow for power."
    };
    
    std::string doc1_id = importTestContent("climate_report.txt", "text/plain", doc1_chunks);
    std::string doc2_id = importTestContent("renewable_energy.txt", "text/plain", doc2_chunks);

    // Search for relevant chunks
    auto search_results = content_mgr->searchContentHybrid(
        "renewable energy climate",
        10,
        {},
        0.7,  // Prefer vector search
        0.3,  // Some fulltext
        60
    );

    ASSERT_FALSE(search_results.empty());

    // Get chunk and trace back to content
    std::string top_chunk_id = search_results[0].first;
    auto chunk_meta = content_mgr->getChunk(top_chunk_id);
    ASSERT_TRUE(chunk_meta.has_value());

    std::string found_content_id = chunk_meta.value().content_id;

    // Assemble full document
    auto assembly = content_mgr->assembleContent(found_content_id, true);
    ASSERT_TRUE(assembly.has_value());
    ASSERT_TRUE(assembly.value().assembled_text.has_value());

    // Verify content
    std::string full_doc = assembly.value().assembled_text.value();
    EXPECT_FALSE(full_doc.empty());
}

TEST_F(ContentFeaturesTest, Integration_FilesystemAndNavigation) {
    // Create directory structure
    content_mgr->createDirectory("/library/books", true);

    // Import book with chapters
    std::vector<std::string> chapters = {
        "Chapter 1: The Beginning",
        "Chapter 2: Rising Action",
        "Chapter 3: The Climax",
        "Chapter 4: Falling Action",
        "Chapter 5: Resolution"
    };

    std::string book_id = importTestContent("novel.txt", "text/plain", chapters);
    content_mgr->registerPath(book_id, "/library/books/scifi_novel.txt");

    // Resolve path
    auto resolved = content_mgr->resolvePath("/library/books/scifi_novel.txt");
    ASSERT_TRUE(resolved.has_value());

    // Get chunks for navigation
    auto all_chunks = content_mgr->getContentChunks(resolved.value().id);
    ASSERT_EQ(all_chunks.size(), 5);

    // Navigate through chapters
    std::string chapter1_id = all_chunks[0].id;
    
    auto chapter2 = content_mgr->getNextChunk(chapter1_id);
    ASSERT_TRUE(chapter2.has_value());
    EXPECT_TRUE(chapter2.value().text.find("Chapter 2") != std::string::npos);

    auto chapter3 = content_mgr->getNextChunk(chapter2.value().id);
    ASSERT_TRUE(chapter3.has_value());
    EXPECT_TRUE(chapter3.value().text.find("Chapter 3") != std::string::npos);

    // Navigate back
    auto back_to_chapter2 = content_mgr->getPreviousChunk(chapter3.value().id);
    ASSERT_TRUE(back_to_chapter2.has_value());
    EXPECT_EQ(back_to_chapter2.value().id, chapter2.value().id);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
