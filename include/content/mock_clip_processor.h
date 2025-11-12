#pragma once

#include "content/content_processor.h"
#include <string>

namespace themis {
namespace content {

// Mock CLIP-like image processor for deterministic embeddings used in tests.
class MockClipProcessor : public IContentProcessor {
public:
    MockClipProcessor(int dim = 512) : dim_(dim) {}
    ~MockClipProcessor() override = default;

    ExtractionResult extract(const std::string& blob, const ContentType& content_type) override;
    std::vector<nlohmann::json> chunk(const ExtractionResult& extraction_result, int chunk_size, int overlap) override;
    std::vector<float> generateEmbedding(const std::string& chunk_data) override;
    std::string getName() const override { return "MockClipProcessor"; }
    std::vector<ContentCategory> getSupportedCategories() const override { return {ContentCategory::IMAGE}; }

private:
    int dim_ = 512;
    std::vector<float> computeMockEmbedding_(const std::string& data) const;
};

} // namespace content
} // namespace themis
