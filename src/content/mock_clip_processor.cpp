#include "content/mock_clip_processor.h"
#include "utils/logger.h"
#include <functional>
#include <cmath>

namespace themis {
namespace content {

ExtractionResult MockClipProcessor::extract(const std::string& blob, const ContentType& content_type) {
    ExtractionResult res;
    res.ok = true;
    res.metadata = nlohmann::json::object();
    res.metadata["mime_type"] = content_type.mime_type;
    res.metadata["original_size_bytes"] = static_cast<int>(blob.size());

    // For images we don't extract text; instead produce a mock embedding
    res.embedding = computeMockEmbedding_(blob);
    return res;
}

std::vector<nlohmann::json> MockClipProcessor::chunk(const ExtractionResult& extraction_result, int /*chunk_size*/, int /*overlap*/) {
    std::vector<nlohmann::json> out;
    nlohmann::json chunk;
    chunk["text"] = "";
    chunk["seq_num"] = 0;
    chunk["token_count"] = 0;
    chunk["embedding"] = extraction_result.embedding;
    out.push_back(chunk);
    return out;
}

std::vector<float> MockClipProcessor::generateEmbedding(const std::string& chunk_data) {
    return computeMockEmbedding_(chunk_data);
}

std::vector<float> MockClipProcessor::computeMockEmbedding_(const std::string& data) const {
    std::vector<float> v(dim_, 0.0f);
    if (data.empty()) return v;

    // Deterministic hash-based pseudo-embedding
    std::hash<std::string> hasher;
    size_t h = hasher(data);

    // Spread influence across dimensions
    for (int i = 0; i < dim_; ++i) {
        // mix hash with index for some variance
        uint64_t mixed = h ^ (static_cast<uint64_t>(i) * 0x9e3779b97f4a7c15ULL);
        // convert to float in [-1,1]
        float val = static_cast<int64_t>(mixed % 100000) / 100000.0f;
        val = (val * 2.0f) - 1.0f;
        v[i] = val;
    }

    // L2 normalize
    double sum = 0.0;
    for (float x : v) sum += static_cast<double>(x) * x;
    double norm = std::sqrt(sum);
    if (norm > 1e-6) {
        for (float &x : v) x = static_cast<float>(x / norm);
    }

    return v;
}

} // namespace content
} // namespace themis
