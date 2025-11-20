#pragma once

#include "acceleration/compute_backend.h"

namespace themis {
namespace acceleration {

// CUDA backend for GPU acceleration (NVIDIA)
// This is a stub implementation that will be fully implemented when CUDA is enabled
class CUDAVectorBackend : public IVectorBackend {
public:
    CUDAVectorBackend() = default;
    ~CUDAVectorBackend() override;
    
    // IComputeBackend interface
    const char* name() const noexcept override { return "CUDA"; }
    BackendType type() const noexcept override { return BackendType::CUDA; }
    bool isAvailable() const noexcept override;
    
    BackendCapabilities getCapabilities() const override;
    bool initialize() override;
    void shutdown() override;
    
    // IVectorBackend interface
    std::vector<float> computeDistances(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        bool useL2 = true
    ) override;
    
    std::vector<std::vector<std::pair<uint32_t, float>>> batchKnnSearch(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        size_t k,
        bool useL2 = true
    ) override;

private:
    bool initialized_ = false;
    void* deviceContext_ = nullptr;  // CUDA context
};

class CUDAGraphBackend : public IGraphBackend {
public:
    CUDAGraphBackend() = default;
    ~CUDAGraphBackend() override;
    
    const char* name() const noexcept override { return "CUDA"; }
    BackendType type() const noexcept override { return BackendType::CUDA; }
    bool isAvailable() const noexcept override;
    
    BackendCapabilities getCapabilities() const override;
    bool initialize() override;
    void shutdown() override;
    
    std::vector<std::vector<uint32_t>> batchBFS(
        const uint32_t* adjacency,
        size_t numVertices,
        const uint32_t* startVertices,
        size_t numStarts,
        uint32_t maxDepth
    ) override;
    
    std::vector<std::vector<uint32_t>> batchShortestPath(
        const uint32_t* adjacency,
        const float* weights,
        size_t numVertices,
        const uint32_t* startVertices,
        const uint32_t* endVertices,
        size_t numPairs
    ) override;

private:
    bool initialized_ = false;
    void* deviceContext_ = nullptr;
};

class CUDAGeoBackend : public IGeoBackend {
public:
    CUDAGeoBackend() = default;
    ~CUDAGeoBackend() override;
    
    const char* name() const noexcept override { return "CUDA"; }
    BackendType type() const noexcept override { return BackendType::CUDA; }
    bool isAvailable() const noexcept override;
    
    BackendCapabilities getCapabilities() const override;
    bool initialize() override;
    void shutdown() override;
    
    std::vector<float> batchDistances(
        const double* latitudes1,
        const double* longitudes1,
        const double* latitudes2,
        const double* longitudes2,
        size_t count,
        bool useHaversine = true
    ) override;
    
    std::vector<bool> batchPointInPolygon(
        const double* pointLats,
        const double* pointLons,
        size_t numPoints,
        const double* polygonCoords,
        size_t numPolygonVertices
    ) override;

private:
    bool initialized_ = false;
    void* deviceContext_ = nullptr;
};

} // namespace acceleration
} // namespace themis
