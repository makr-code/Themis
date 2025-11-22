#include "acceleration/cuda_backend.h"
#include <iostream>
#include <sstream>
#include <algorithm>

#ifdef THEMIS_ENABLE_CUDA
#include <cuda_runtime.h>

// External CUDA kernel declarations
extern "C" {
void launchL2DistanceKernel(
    const float* d_queries,
    const float* d_vectors,
    float* d_distances,
    int numQueries,
    int numVectors,
    int dim,
    cudaStream_t stream
);

void launchCosineDistanceKernel(
    const float* d_queries,
    const float* d_vectors,
    float* d_distances,
    int numQueries,
    int numVectors,
    int dim,
    cudaStream_t stream
);

void launchTopKKernel(
    const float* d_distances,
    int* d_topkIndices,
    float* d_topkDistances,
    int numQueries,
    int numVectors,
    int k,
    cudaStream_t stream
);
}

#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            std::cerr << "CUDA error in " << __FILE__ << ":" << __LINE__ \
                      << " - " << cudaGetErrorString(err) << std::endl; \
            return {}; \
        } \
    } while(0)

#define CUDA_CHECK_BOOL(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            std::cerr << "CUDA error in " << __FILE__ << ":" << __LINE__ \
                      << " - " << cudaGetErrorString(err) << std::endl; \
            return false; \
        } \
    } while(0)

#endif

namespace themis {
namespace acceleration {

// ============================================================================
// CUDAVectorBackend Implementation
// ============================================================================

CUDAVectorBackend::~CUDAVectorBackend() {
    shutdown();
}

bool CUDAVectorBackend::isAvailable() const noexcept {
#ifdef THEMIS_ENABLE_CUDA
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    return (err == cudaSuccess && deviceCount > 0);
#else
    return false;
#endif
}

BackendCapabilities CUDAVectorBackend::getCapabilities() const {
    BackendCapabilities caps;
#ifdef THEMIS_ENABLE_CUDA
    caps.supportsVectorOps = true;
    caps.supportsGraphOps = false;
    caps.supportsGeoOps = false;
    caps.supportsBatchProcessing = true;
    caps.supportsAsync = true;
    
    if (isAvailable()) {
        cudaDeviceProp prop;
        if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
            caps.deviceName = std::string(prop.name);
            caps.maxMemoryBytes = prop.totalGlobalMem;
            caps.computeUnits = prop.multiProcessorCount;
        }
    } else {
        caps.deviceName = "CUDA Device (Not Available)";
    }
#endif
    return caps;
}

bool CUDAVectorBackend::initialize() {
#ifdef THEMIS_ENABLE_CUDA
    if (!isAvailable()) {
        std::cerr << "CUDA: No CUDA-capable device found" << std::endl;
        return false;
    }
    
    // Set device
    CUDA_CHECK_BOOL(cudaSetDevice(0));
    
    // Create CUDA stream for async operations
    cudaStream_t stream;
    CUDA_CHECK_BOOL(cudaStreamCreate(&stream));
    deviceContext_ = static_cast<void*>(stream);
    
    // Query device properties
    cudaDeviceProp prop;
    CUDA_CHECK_BOOL(cudaGetDeviceProperties(&prop, 0));
    
    std::cout << "CUDA Backend initialized successfully:" << std::endl;
    std::cout << "  Device: " << prop.name << std::endl;
    std::cout << "  Compute Capability: " << prop.major << "." << prop.minor << std::endl;
    std::cout << "  Global Memory: " << (prop.totalGlobalMem / (1024*1024*1024)) << " GB" << std::endl;
    std::cout << "  Multiprocessors: " << prop.multiProcessorCount << std::endl;
    
    initialized_ = true;
    return true;
#else
    return false;
#endif
}

void CUDAVectorBackend::shutdown() {
#ifdef THEMIS_ENABLE_CUDA
    if (initialized_) {
        if (deviceContext_) {
            cudaStream_t stream = static_cast<cudaStream_t>(deviceContext_);
            cudaStreamDestroy(stream);
            deviceContext_ = nullptr;
        }
        cudaDeviceReset();
        initialized_ = false;
    }
#endif
}

std::vector<float> CUDAVectorBackend::computeDistances(
    const float* queries,
    size_t numQueries,
    size_t dim,
    const float* vectors,
    size_t numVectors,
    bool useL2
) {
#ifdef THEMIS_ENABLE_CUDA
    if (!initialized_) {
        std::cerr << "CUDA backend not initialized" << std::endl;
        return {};
    }
    
    cudaStream_t stream = static_cast<cudaStream_t>(deviceContext_);
    
    // Allocate device memory
    float *d_queries, *d_vectors, *d_distances;
    size_t querySize = numQueries * dim * sizeof(float);
    size_t vectorSize = numVectors * dim * sizeof(float);
    size_t distanceSize = numQueries * numVectors * sizeof(float);
    
    CUDA_CHECK(cudaMalloc(&d_queries, querySize));
    CUDA_CHECK(cudaMalloc(&d_vectors, vectorSize));
    CUDA_CHECK(cudaMalloc(&d_distances, distanceSize));
    
    // Copy data to device
    CUDA_CHECK(cudaMemcpyAsync(d_queries, queries, querySize, cudaMemcpyHostToDevice, stream));
    CUDA_CHECK(cudaMemcpyAsync(d_vectors, vectors, vectorSize, cudaMemcpyHostToDevice, stream));
    
    // Launch kernel
    if (useL2) {
        launchL2DistanceKernel(d_queries, d_vectors, d_distances,
                              numQueries, numVectors, dim, stream);
    } else {
        launchCosineDistanceKernel(d_queries, d_vectors, d_distances,
                                  numQueries, numVectors, dim, stream);
    }
    
    // Copy results back
    std::vector<float> distances(numQueries * numVectors);
    CUDA_CHECK(cudaMemcpyAsync(distances.data(), d_distances, distanceSize,
                               cudaMemcpyDeviceToHost, stream));
    
    // Synchronize
    CUDA_CHECK(cudaStreamSynchronize(stream));
    
    // Cleanup
    cudaFree(d_queries);
    cudaFree(d_vectors);
    cudaFree(d_distances);
    
    return distances;
#else
    return {};
#endif
}

std::vector<std::vector<std::pair<uint32_t, float>>> CUDAVectorBackend::batchKnnSearch(
    const float* queries,
    size_t numQueries,
    size_t dim,
    const float* vectors,
    size_t numVectors,
    size_t k,
    bool useL2
) {
#ifdef THEMIS_ENABLE_CUDA
    if (!initialized_) {
        std::cerr << "CUDA backend not initialized" << std::endl;
        return {};
    }
    
    cudaStream_t stream = static_cast<cudaStream_t>(deviceContext_);
    
    // Allocate device memory
    float *d_queries, *d_vectors, *d_distances;
    int *d_topkIndices;
    float *d_topkDistances;
    
    size_t querySize = numQueries * dim * sizeof(float);
    size_t vectorSize = numVectors * dim * sizeof(float);
    size_t distanceSize = numQueries * numVectors * sizeof(float);
    size_t topkIdxSize = numQueries * k * sizeof(int);
    size_t topkDistSize = numQueries * k * sizeof(float);
    
    CUDA_CHECK(cudaMalloc(&d_queries, querySize));
    CUDA_CHECK(cudaMalloc(&d_vectors, vectorSize));
    CUDA_CHECK(cudaMalloc(&d_distances, distanceSize));
    CUDA_CHECK(cudaMalloc(&d_topkIndices, topkIdxSize));
    CUDA_CHECK(cudaMalloc(&d_topkDistances, topkDistSize));
    
    // Copy data to device
    CUDA_CHECK(cudaMemcpyAsync(d_queries, queries, querySize, cudaMemcpyHostToDevice, stream));
    CUDA_CHECK(cudaMemcpyAsync(d_vectors, vectors, vectorSize, cudaMemcpyHostToDevice, stream));
    
    // Step 1: Compute distances
    if (useL2) {
        launchL2DistanceKernel(d_queries, d_vectors, d_distances,
                              numQueries, numVectors, dim, stream);
    } else {
        launchCosineDistanceKernel(d_queries, d_vectors, d_distances,
                                  numQueries, numVectors, dim, stream);
    }
    
    // Step 2: Extract top-k
    launchTopKKernel(d_distances, d_topkIndices, d_topkDistances,
                    numQueries, numVectors, k, stream);
    
    // Copy results back
    std::vector<int> topkIndices(numQueries * k);
    std::vector<float> topkDistances(numQueries * k);
    
    CUDA_CHECK(cudaMemcpyAsync(topkIndices.data(), d_topkIndices, topkIdxSize,
                               cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaMemcpyAsync(topkDistances.data(), d_topkDistances, topkDistSize,
                               cudaMemcpyDeviceToHost, stream));
    
    // Synchronize
    CUDA_CHECK(cudaStreamSynchronize(stream));
    
    // Cleanup device memory
    cudaFree(d_queries);
    cudaFree(d_vectors);
    cudaFree(d_distances);
    cudaFree(d_topkIndices);
    cudaFree(d_topkDistances);
    
    // Convert to output format
    std::vector<std::vector<std::pair<uint32_t, float>>> results(numQueries);
    for (size_t q = 0; q < numQueries; ++q) {
        results[q].reserve(k);
        for (size_t i = 0; i < k; ++i) {
            size_t idx = q * k + i;
            results[q].emplace_back(
                static_cast<uint32_t>(topkIndices[idx]),
                topkDistances[idx]
            );
        }
    }
    
    return results;
#else
    return {};
#endif
}

// ============================================================================
// CUDAGraphBackend Stub Implementation
// ============================================================================

CUDAGraphBackend::~CUDAGraphBackend() {
    shutdown();
}

bool CUDAGraphBackend::isAvailable() const noexcept {
#ifdef THEMIS_ENABLE_CUDA
    return false; // Stub
#else
    return false;
#endif
}

BackendCapabilities CUDAGraphBackend::getCapabilities() const {
    BackendCapabilities caps;
#ifdef THEMIS_ENABLE_CUDA
    caps.supportsGraphOps = true;
    caps.deviceName = "CUDA Device (Stub)";
#endif
    return caps;
}

bool CUDAGraphBackend::initialize() {
#ifdef THEMIS_ENABLE_CUDA
    initialized_ = false; // Stub
    return initialized_;
#else
    return false;
#endif
}

void CUDAGraphBackend::shutdown() {
#ifdef THEMIS_ENABLE_CUDA
    initialized_ = false;
#endif
}

std::vector<std::vector<uint32_t>> CUDAGraphBackend::batchBFS(
    const uint32_t* /*adjacency*/,
    size_t /*numVertices*/,
    const uint32_t* /*startVertices*/,
    size_t /*numStarts*/,
    uint32_t /*maxDepth*/
) {
    return {}; // Stub
}

std::vector<std::vector<uint32_t>> CUDAGraphBackend::batchShortestPath(
    const uint32_t* /*adjacency*/,
    const float* /*weights*/,
    size_t /*numVertices*/,
    const uint32_t* /*startVertices*/,
    const uint32_t* /*endVertices*/,
    size_t /*numPairs*/
) {
    return {}; // Stub
}

// ============================================================================
// CUDAGeoBackend Stub Implementation
// ============================================================================

CUDAGeoBackend::~CUDAGeoBackend() {
    shutdown();
}

bool CUDAGeoBackend::isAvailable() const noexcept {
#ifdef THEMIS_ENABLE_CUDA
    return false; // Stub
#else
    return false;
#endif
}

BackendCapabilities CUDAGeoBackend::getCapabilities() const {
    BackendCapabilities caps;
#ifdef THEMIS_ENABLE_CUDA
    caps.supportsGeoOps = true;
    caps.deviceName = "CUDA Device (Stub)";
#endif
    return caps;
}

bool CUDAGeoBackend::initialize() {
#ifdef THEMIS_ENABLE_CUDA
    initialized_ = false; // Stub
    return initialized_;
#else
    return false;
#endif
}

void CUDAGeoBackend::shutdown() {
#ifdef THEMIS_ENABLE_CUDA
    initialized_ = false;
#endif
}

std::vector<float> CUDAGeoBackend::batchDistances(
    const double* /*latitudes1*/,
    const double* /*longitudes1*/,
    const double* /*latitudes2*/,
    const double* /*longitudes2*/,
    size_t /*count*/,
    bool /*useHaversine*/
) {
    return {}; // Stub
}

std::vector<bool> CUDAGeoBackend::batchPointInPolygon(
    const double* /*pointLats*/,
    const double* /*pointLons*/,
    size_t /*numPoints*/,
    const double* /*polygonCoords*/,
    size_t /*numPolygonVertices*/
) {
    return {}; // Stub
}

} // namespace acceleration
} // namespace themis
