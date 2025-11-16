#include <gtest/gtest.h>
#include "content/content_processor.h"
#include "content/content_type.h"
#include <cmath>

using namespace themis::content;

class TextProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        processor = std::make_unique<TextProcessor>();
        
        // Initialize default content types
        initializeDefaultContentTypes();
    }
    
    std::unique_ptr<TextProcessor> processor;
};

// ============================================================================
// Extract Tests
// ============================================================================

TEST_F(TextProcessorTest, ExtractPlainText) {
    std::string blob = "Hello, world! This is a test.";
    const ContentType* type = ContentTypeRegistry::instance().getByMimeType("text/plain");
    ASSERT_NE(type, nullptr);
    
    auto result = processor->extract(blob, *type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.text, "Hello, world! This is a test.");
    EXPECT_TRUE(result.metadata.contains("token_count"));
    EXPECT_GT(result.metadata["token_count"].get<int>(), 0);
}

TEST_F(TextProcessorTest, ExtractNormalizesWhitespace) {
    std::string blob = "Hello,    world!  \n\n  This   is a test.";
    const ContentType* type = ContentTypeRegistry::instance().getByMimeType("text/plain");
    
    auto result = processor->extract(blob, *type);
    
    ASSERT_TRUE(result.ok);
    // Multiple spaces should be normalized to single space
    EXPECT_NE(result.text.find("Hello, world!"), std::string::npos);
    EXPECT_NE(result.text.find("This is a test."), std::string::npos);
}

TEST_F(TextProcessorTest, ExtractRemovesCarriageReturns) {
    std::string blob = "Hello,\r\nworld!\r\nThis is a test.";
    const ContentType* type = ContentTypeRegistry::instance().getByMimeType("text/plain");
    
    auto result = processor->extract(blob, *type);
    
    ASSERT_TRUE(result.ok);
    // \r should be removed
    EXPECT_EQ(result.text.find('\r'), std::string::npos);
}

TEST_F(TextProcessorTest, ExtractCodeDetectsLanguage) {
    std::string blob = "def hello():\n    print('Hello, world!')";
    const ContentType* type = ContentTypeRegistry::instance().getByMimeType("text/x-python");
    ASSERT_NE(type, nullptr);
    
    auto result = processor->extract(blob, *type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.metadata.contains("language"));
    EXPECT_EQ(result.metadata["language"].get<std::string>(), "python");
    EXPECT_TRUE(result.metadata["is_code"].get<bool>());
    EXPECT_TRUE(result.metadata.contains("line_count"));
}

TEST_F(TextProcessorTest, ExtractCountsTokens) {
    std::string blob = "This is a simple test with tokens.";
    const ContentType* type = ContentTypeRegistry::instance().getByMimeType("text/plain");
    
    auto result = processor->extract(blob, *type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.metadata.contains("token_count"));
    EXPECT_EQ(result.metadata["token_count"].get<int>(), 7); // 7 words
}

TEST_F(TextProcessorTest, ExtractCountsSentences) {
    std::string blob = "First sentence. Second sentence! Third sentence?";
    const ContentType* type = ContentTypeRegistry::instance().getByMimeType("text/plain");
    
    auto result = processor->extract(blob, *type);
    
    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.metadata.contains("sentence_count"));
    EXPECT_EQ(result.metadata["sentence_count"].get<int>(), 3);
}

// ============================================================================
// Chunk Tests
// ============================================================================

TEST_F(TextProcessorTest, ChunkSimpleText) {
    ExtractionResult extraction;
    extraction.text = "First sentence. Second sentence. Third sentence. Fourth sentence.";
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 3, 0); // 3 tokens per chunk, no overlap
    
    ASSERT_GE(chunks.size(), 2);
    
    // Verify all chunks have text
    for (const auto& chunk : chunks) {
        EXPECT_TRUE(chunk.contains("text"));
        EXPECT_FALSE(chunk["text"].get<std::string>().empty());
        EXPECT_TRUE(chunk.contains("seq_num"));
        EXPECT_TRUE(chunk.contains("token_count"));
    }
}

TEST_F(TextProcessorTest, ChunkWithOverlap) {
    ExtractionResult extraction;
    extraction.text = "First sentence. Second sentence. Third sentence. Fourth sentence.";
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 3, 1); // 3 tokens per chunk, 1 token overlap
    
    ASSERT_GE(chunks.size(), 2);
    
    // Verify seq_num is sequential
    for (size_t i = 0; i < chunks.size(); i++) {
        EXPECT_EQ(chunks[i]["seq_num"].get<int>(), static_cast<int>(i));
    }
}

TEST_F(TextProcessorTest, ChunkEmptyText) {
    ExtractionResult extraction;
    extraction.text = "";
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 512, 50);
    
    EXPECT_EQ(chunks.size(), 0);
}

TEST_F(TextProcessorTest, ChunkSingleSentence) {
    ExtractionResult extraction;
    extraction.text = "This is a single sentence.";
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 512, 50);
    
    ASSERT_EQ(chunks.size(), 1);
    EXPECT_EQ(chunks[0]["text"].get<std::string>(), "This is a single sentence.");
    EXPECT_EQ(chunks[0]["seq_num"].get<int>(), 0);
}

TEST_F(TextProcessorTest, ChunkLargeSentenceExceedingChunkSize) {
    ExtractionResult extraction;
    // Create a very long sentence (100 words)
    std::string long_sentence = "This";
    for (int i = 0; i < 99; i++) {
        long_sentence += " word";
    }
    long_sentence += ".";
    extraction.text = long_sentence;
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 10, 0); // 10 tokens max
    
    // Should still create a chunk even though sentence exceeds size
    ASSERT_GE(chunks.size(), 1);
    EXPECT_FALSE(chunks[0]["text"].get<std::string>().empty());
}

TEST_F(TextProcessorTest, ChunkPreservesSentenceBoundaries) {
    ExtractionResult extraction;
    extraction.text = "Short. Another short. And another. Final sentence here.";
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 5, 0); // 5 tokens per chunk
    
    ASSERT_GE(chunks.size(), 1);
    
    // Verify chunks are non-empty and contain text
    for (const auto& chunk : chunks) {
        std::string text = chunk["text"].get<std::string>();
        EXPECT_FALSE(text.empty());
        // Sentence boundaries are preserved (each chunk contains complete sentences)
        EXPECT_TRUE(text.find('.') != std::string::npos || chunks.back() == chunk);
    }
}

TEST_F(TextProcessorTest, ChunkOffsetsAreCorrect) {
    ExtractionResult extraction;
    extraction.text = "First. Second. Third.";
    extraction.ok = true;
    
    auto chunks = processor->chunk(extraction, 2, 0);
    
    ASSERT_GE(chunks.size(), 1);
    
    // First chunk should start at offset 0
    EXPECT_EQ(chunks[0]["start_offset"].get<int>(), 0);
    
    // End offset should be <= text length
    for (const auto& chunk : chunks) {
        int end = chunk["end_offset"].get<int>();
        EXPECT_LE(end, static_cast<int>(extraction.text.size()));
    }
}

// ============================================================================
// Embedding Tests
// ============================================================================

TEST_F(TextProcessorTest, GenerateEmbeddingReturns768Dimensions) {
    std::string text = "Hello, world!";
    
    auto embedding = processor->generateEmbedding(text);
    
    EXPECT_EQ(embedding.size(), 768);
}

TEST_F(TextProcessorTest, GenerateEmbeddingIsNormalized) {
    std::string text = "This is a test sentence.";
    
    auto embedding = processor->generateEmbedding(text);
    
    // Calculate L2 norm
    float norm = 0.0f;
    for (float val : embedding) {
        norm += val * val;
    }
    norm = std::sqrt(norm);
    
    // Should be approximately 1.0 (unit vector)
    EXPECT_NEAR(norm, 1.0f, 1e-5f);
}

TEST_F(TextProcessorTest, GenerateEmbeddingIsDeterministic) {
    std::string text = "Deterministic test.";
    
    auto embedding1 = processor->generateEmbedding(text);
    auto embedding2 = processor->generateEmbedding(text);
    
    // Same input should produce same embedding
    ASSERT_EQ(embedding1.size(), embedding2.size());
    for (size_t i = 0; i < embedding1.size(); i++) {
        EXPECT_FLOAT_EQ(embedding1[i], embedding2[i]);
    }
}

TEST_F(TextProcessorTest, GenerateEmbeddingDifferentForDifferentText) {
    std::string text1 = "First text.";
    std::string text2 = "Second text.";
    
    auto embedding1 = processor->generateEmbedding(text1);
    auto embedding2 = processor->generateEmbedding(text2);
    
    // Different inputs should produce different embeddings
    bool different = false;
    for (size_t i = 0; i < embedding1.size(); i++) {
        if (std::abs(embedding1[i] - embedding2[i]) > 1e-6f) {
            different = true;
            break;
        }
    }
    EXPECT_TRUE(different);
}

TEST_F(TextProcessorTest, GenerateEmbeddingHandlesEmptyString) {
    std::string text = "";
    
    auto embedding = processor->generateEmbedding(text);
    
    EXPECT_EQ(embedding.size(), 768);
    // All zeros (normalized to zero vector)
    float sum = 0.0f;
    for (float val : embedding) {
        sum += std::abs(val);
    }
    EXPECT_FLOAT_EQ(sum, 0.0f);
}

// ============================================================================
// Supported Categories Test
// ============================================================================

TEST_F(TextProcessorTest, SupportsTextCategory) {
    auto categories = processor->getSupportedCategories();
    
    ASSERT_EQ(categories.size(), 1);
    EXPECT_EQ(categories[0], ContentCategory::TEXT);
}

TEST_F(TextProcessorTest, ProcessorName) {
    EXPECT_EQ(processor->getName(), "TextProcessor");
}
