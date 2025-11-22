#include "acceleration/cpu_backend.h"
#include <cmath>
#include <algorithm>
#include <queue>
#include <limits>

namespace themis {
namespace acceleration {

// ============================================================================
// CPUVectorBackend Implementation
// ============================================================================

float CPUVectorBackend::computeL2Distance(const float* a, const float* b, size_t dim) const {
    float sum = 0.0f;
    for (size_t i = 0; i < dim; ++i) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }
    return std::sqrt(sum);
}

float CPUVectorBackend::computeCosineDistance(const float* a, const float* b, size_t dim) const {
    float dotProduct = 0.0f;
    float normA = 0.0f;
    float normB = 0.0f;
    
    for (size_t i = 0; i < dim; ++i) {
        dotProduct += a[i] * b[i];
        normA += a[i] * a[i];
        normB += b[i] * b[i];
    }
    
    normA = std::sqrt(normA);
    normB = std::sqrt(normB);
    
    if (normA < 1e-10f || normB < 1e-10f) {
        return 1.0f; // Maximum distance for zero vectors
    }
    
    float cosine = dotProduct / (normA * normB);
    return 1.0f - cosine; // Convert similarity to distance
}

std::vector<float> CPUVectorBackend::computeDistances(
    const float* queries,
    size_t numQueries,
    size_t dim,
    const float* vectors,
    size_t numVectors,
    bool useL2
) {
    std::vector<float> distances(numQueries * numVectors);
    
    for (size_t q = 0; q < numQueries; ++q) {
        const float* query = queries + q * dim;
        for (size_t v = 0; v < numVectors; ++v) {
            const float* vector = vectors + v * dim;
            float dist = useL2 ? computeL2Distance(query, vector, dim)
                              : computeCosineDistance(query, vector, dim);
            distances[q * numVectors + v] = dist;
        }
    }
    
    return distances;
}

std::vector<std::vector<std::pair<uint32_t, float>>> CPUVectorBackend::batchKnnSearch(
    const float* queries,
    size_t numQueries,
    size_t dim,
    const float* vectors,
    size_t numVectors,
    size_t k,
    bool useL2
) {
    std::vector<std::vector<std::pair<uint32_t, float>>> results(numQueries);
    
    for (size_t q = 0; q < numQueries; ++q) {
        const float* query = queries + q * dim;
        
        // Compute all distances for this query
        std::vector<std::pair<uint32_t, float>> distances;
        distances.reserve(numVectors);
        
        for (size_t v = 0; v < numVectors; ++v) {
            const float* vector = vectors + v * dim;
            float dist = useL2 ? computeL2Distance(query, vector, dim)
                              : computeCosineDistance(query, vector, dim);
            distances.emplace_back(static_cast<uint32_t>(v), dist);
        }
        
        // Partial sort to get k nearest neighbors
        size_t actualK = std::min(k, distances.size());
        std::partial_sort(
            distances.begin(),
            distances.begin() + actualK,
            distances.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; }
        );
        
        results[q].assign(distances.begin(), distances.begin() + actualK);
    }
    
    return results;
}

// ============================================================================
// CPUGraphBackend Implementation
// ============================================================================

std::vector<std::vector<uint32_t>> CPUGraphBackend::batchBFS(
    const uint32_t* adjacency,
    size_t numVertices,
    const uint32_t* startVertices,
    size_t numStarts,
    uint32_t maxDepth
) {
    std::vector<std::vector<uint32_t>> results(numStarts);
    
    for (size_t s = 0; s < numStarts; ++s) {
        uint32_t start = startVertices[s];
        std::vector<bool> visited(numVertices, false);
        std::queue<std::pair<uint32_t, uint32_t>> queue; // (vertex, depth)
        
        queue.push({start, 0});
        visited[start] = true;
        results[s].push_back(start);
        
        while (!queue.empty()) {
            auto [current, depth] = queue.front();
            queue.pop();
            
            if (depth >= maxDepth) {
                continue;
            }
            
            // Note: This assumes adjacency is stored as an offset array
            // In a real implementation, you'd need a proper adjacency list structure
            // For now, this is a simplified placeholder
        }
    }
    
    return results;
}

std::vector<std::vector<uint32_t>> CPUGraphBackend::batchShortestPath(
    const uint32_t* adjacency,
    const float* weights,
    size_t numVertices,
    const uint32_t* startVertices,
    const uint32_t* endVertices,
    size_t numPairs
) {
    std::vector<std::vector<uint32_t>> results(numPairs);
    
    // Simplified Dijkstra implementation placeholder
    // Full implementation would require proper graph data structures
    
    return results;
}

// ============================================================================
// CPUGeoBackend Implementation
// ============================================================================

constexpr double EARTH_RADIUS_KM = 6371.0;
constexpr double PI = 3.14159265358979323846;

double CPUGeoBackend::haversineDistance(double lat1, double lon1, double lat2, double lon2) const {
    // Convert degrees to radians
    lat1 = lat1 * PI / 180.0;
    lon1 = lon1 * PI / 180.0;
    lat2 = lat2 * PI / 180.0;
    lon2 = lon2 * PI / 180.0;
    
    double dlat = lat2 - lat1;
    double dlon = lon2 - lon1;
    
    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1) * std::cos(lat2) *
               std::sin(dlon / 2) * std::sin(dlon / 2);
    
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    
    return EARTH_RADIUS_KM * c;
}

double CPUGeoBackend::vincentyDistance(double lat1, double lon1, double lat2, double lon2) const {
    // Simplified Vincenty formula (more accurate than Haversine)
    // Full implementation would include ellipsoid parameters
    return haversineDistance(lat1, lon1, lat2, lon2);
}

std::vector<float> CPUGeoBackend::batchDistances(
    const double* latitudes1,
    const double* longitudes1,
    const double* latitudes2,
    const double* longitudes2,
    size_t count,
    bool useHaversine
) {
    std::vector<float> distances(count);
    
    for (size_t i = 0; i < count; ++i) {
        double dist = useHaversine 
            ? haversineDistance(latitudes1[i], longitudes1[i], latitudes2[i], longitudes2[i])
            : vincentyDistance(latitudes1[i], longitudes1[i], latitudes2[i], longitudes2[i]);
        distances[i] = static_cast<float>(dist);
    }
    
    return distances;
}

std::vector<bool> CPUGeoBackend::batchPointInPolygon(
    const double* pointLats,
    const double* pointLons,
    size_t numPoints,
    const double* polygonCoords,
    size_t numPolygonVertices
) {
    std::vector<bool> results(numPoints);
    
    // Ray casting algorithm for point-in-polygon test
    for (size_t p = 0; p < numPoints; ++p) {
        double testLat = pointLats[p];
        double testLon = pointLons[p];
        
        bool inside = false;
        size_t j = numPolygonVertices - 1;
        
        for (size_t i = 0; i < numPolygonVertices; ++i) {
            double lat_i = polygonCoords[i * 2];
            double lon_i = polygonCoords[i * 2 + 1];
            double lat_j = polygonCoords[j * 2];
            double lon_j = polygonCoords[j * 2 + 1];
            
            if (((lon_i > testLon) != (lon_j > testLon)) &&
                (testLat < (lat_j - lat_i) * (testLon - lon_i) / (lon_j - lon_i) + lat_i)) {
                inside = !inside;
            }
            
            j = i;
        }
        
        results[p] = inside;
    }
    
    return results;
}

} // namespace acceleration
} // namespace themis
