// HIP Backend Implementation for AMD GPUs
// Provides GPU acceleration using AMD ROCm/HIP platform
// Compatible with AMD Radeon GPUs

#include "acceleration/compute_backend.h"
#include <iostream>
#include <vector>
#include <memory>

#ifdef THEMIS_ENABLE_HIP
#include <hip/hip_runtime.h>

namespace themis {
namespace acceleration {

// ============================================================================
// HIP Helper Macros
// ============================================================================

#define HIP_CHECK(call) \
    do { \
        hipError_t error = call; \
        if (error != hipSuccess) { \
            std::cerr << "HIP error in " << __FILE__ << ":" << __LINE__ \
                      << " - " << hipGetErrorString(error) << std::endl; \
            return false; \
        } \
    } while(0)

#define HIP_CHECK_THROW(call) \
    do { \
        hipError_t error = call; \
        if (error != hipSuccess) { \
            throw std::runtime_error(std::string("HIP error: ") + hipGetErrorString(error)); \
        } \
    } while(0)

// ============================================================================
// HIP Kernels (similar to CUDA)
// ============================================================================

// Compute L2 distance kernel
__global__ void computeL2DistanceKernel(
    const float* __restrict__ queries,
    const float* __restrict__ vectors,
    float* __restrict__ distances,
    int numQueries,
    int numVectors,
    int dim
) {
    int qIdx = blockIdx.y * blockDim.y + threadIdx.y;
    int vIdx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (qIdx >= numQueries || vIdx >= numVectors) return;
    
    const float* query = queries + qIdx * dim;
    const float* vector = vectors + vIdx * dim;
    
    float sum = 0.0f;
    
    #pragma unroll 4
    for (int i = 0; i < dim; i++) {
        float diff = query[i] - vector[i];
        sum += diff * diff;
    }
    
    distances[qIdx * numVectors + vIdx] = sqrtf(sum);
}

// Compute Cosine distance kernel
__global__ void computeCosineDistanceKernel(
    const float* __restrict__ queries,
    const float* __restrict__ vectors,
    float* __restrict__ distances,
    int numQueries,
    int numVectors,
    int dim
) {
    int qIdx = blockIdx.y * blockDim.y + threadIdx.y;
    int vIdx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (qIdx >= numQueries || vIdx >= numVectors) return;
    
    const float* query = queries + qIdx * dim;
    const float* vector = vectors + vIdx * dim;
    
    float dotProduct = 0.0f;
    float normQuery = 0.0f;
    float normVector = 0.0f;
    
    #pragma unroll 4
    for (int i = 0; i < dim; i++) {
        float q = query[i];
        float v = vector[i];
        dotProduct += q * v;
        normQuery += q * q;
        normVector += v * v;
    }
    
    normQuery = sqrtf(normQuery);
    normVector = sqrtf(normVector);
    
    float cosineSim = (normQuery > 1e-10f && normVector > 1e-10f)
        ? dotProduct / (normQuery * normVector)
        : 0.0f;
    
    distances[qIdx * numVectors + vIdx] = 1.0f - cosineSim;
}

// ============================================================================
// HIPVectorBackend Implementation
// ============================================================================

class HIPVectorBackend : public IVectorBackend {
public:
    HIPVectorBackend() = default;
    ~HIPVectorBackend() override { shutdown(); }
    
    const char* name() const noexcept override { return "HIP"; }
    BackendType type() const noexcept override { return BackendType::HIP; }
    
    bool isAvailable() const noexcept override {
        int deviceCount = 0;
        hipError_t error = hipGetDeviceCount(&deviceCount);
        return (error == hipSuccess && deviceCount > 0);
    }
    
    BackendCapabilities getCapabilities() const override {
        BackendCapabilities caps;
        caps.supportsVectorOps = true;
        caps.supportsGraphOps = false;
        caps.supportsGeoOps = false;
        caps.supportsBatchProcessing = true;
        caps.supportsAsync = true;
        
        if (initialized_) {
            hipDeviceProp_t prop;
            hipGetDeviceProperties(&prop, deviceId_);
            caps.deviceName = std::string(prop.name) + " (HIP)";
            caps.totalMemory = prop.totalGlobalMem;
            caps.maxBatchSize = 10000;
        } else {
            caps.deviceName = "AMD GPU (HIP - not initialized)";
        }
        
        return caps;
    }
    
    bool initialize() override {
        if (initialized_) return true;
        
        std::cout << "HIP Backend: Initializing..." << std::endl;
        
        int deviceCount = 0;
        HIP_CHECK(hipGetDeviceCount(&deviceCount));
        
        if (deviceCount == 0) {
            std::cerr << "No HIP-capable AMD GPUs found" << std::endl;
            return false;
        }
        
        // Select device with most compute units
        int bestDevice = 0;
        int maxCUs = 0;
        
        for (int i = 0; i < deviceCount; i++) {
            hipDeviceProp_t prop;
            hipGetDeviceProperties(&prop, i);
            std::cout << "Device " << i << ": " << prop.name 
                      << " (" << prop.multiProcessorCount << " CUs)" << std::endl;
            
            if (prop.multiProcessorCount > maxCUs) {
                maxCUs = prop.multiProcessorCount;
                bestDevice = i;
            }
        }
        
        deviceId_ = bestDevice;
        HIP_CHECK(hipSetDevice(deviceId_));
        
        hipDeviceProp_t prop;
        hipGetDeviceProperties(&prop, deviceId_);
        
        std::cout << "HIP Backend: Selected device " << deviceId_ 
                  << " (" << prop.name << ")" << std::endl;
        std::cout << "  Compute Units: " << prop.multiProcessorCount << std::endl;
        std::cout << "  Global Memory: " << (prop.totalGlobalMem / (1024*1024*1024)) << " GB" << std::endl;
        std::cout << "  Warp Size: " << prop.warpSize << std::endl;
        
        // Create stream for async operations
        HIP_CHECK(hipStreamCreate(&stream_));
        
        initialized_ = true;
        return true;
    }
    
    void shutdown() override {
        if (initialized_) {
            hipStreamDestroy(stream_);
            initialized_ = false;
        }
    }
    
    std::vector<float> computeDistances(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        bool useL2 = true
    ) override {
        if (!initialized_) {
            std::cerr << "HIP backend not initialized" << std::endl;
            return {};
        }
        
        // Allocate device memory
        float *d_queries = nullptr, *d_vectors = nullptr, *d_distances = nullptr;
        
        size_t queriesSize = numQueries * dim * sizeof(float);
        size_t vectorsSize = numVectors * dim * sizeof(float);
        size_t distancesSize = numQueries * numVectors * sizeof(float);
        
        try {
            HIP_CHECK_THROW(hipMalloc(&d_queries, queriesSize));
            HIP_CHECK_THROW(hipMalloc(&d_vectors, vectorsSize));
            HIP_CHECK_THROW(hipMalloc(&d_distances, distancesSize));
            
            // Copy data to device
            HIP_CHECK_THROW(hipMemcpy(d_queries, queries, queriesSize, hipMemcpyHostToDevice));
            HIP_CHECK_THROW(hipMemcpy(d_vectors, vectors, vectorsSize, hipMemcpyHostToDevice));
            
            // Launch kernel
            dim3 blockSize(16, 16);
            dim3 gridSize(
                (numVectors + blockSize.x - 1) / blockSize.x,
                (numQueries + blockSize.y - 1) / blockSize.y
            );
            
            if (useL2) {
                hipLaunchKernelGGL(computeL2DistanceKernel, gridSize, blockSize, 0, stream_,
                    d_queries, d_vectors, d_distances,
                    numQueries, numVectors, dim
                );
            } else {
                hipLaunchKernelGGL(computeCosineDistanceKernel, gridSize, blockSize, 0, stream_,
                    d_queries, d_vectors, d_distances,
                    numQueries, numVectors, dim
                );
            }
            
            HIP_CHECK_THROW(hipStreamSynchronize(stream_));
            
            // Copy results back
            std::vector<float> distances(numQueries * numVectors);
            HIP_CHECK_THROW(hipMemcpy(distances.data(), d_distances, distancesSize, hipMemcpyDeviceToHost));
            
            // Cleanup
            hipFree(d_queries);
            hipFree(d_vectors);
            hipFree(d_distances);
            
            return distances;
            
        } catch (const std::exception& e) {
            if (d_queries) hipFree(d_queries);
            if (d_vectors) hipFree(d_vectors);
            if (d_distances) hipFree(d_distances);
            std::cerr << "HIP computeDistances error: " << e.what() << std::endl;
            return {};
        }
    }
    
    std::vector<std::vector<std::pair<uint32_t, float>>> batchKnnSearch(
        const float* queries,
        size_t numQueries,
        size_t dim,
        const float* vectors,
        size_t numVectors,
        size_t k,
        bool useL2 = true
    ) override {
        // Compute all distances first
        auto distances = computeDistances(queries, numQueries, dim, vectors, numVectors, useL2);
        
        if (distances.empty()) {
            return {};
        }
        
        // CPU-based top-k selection (could be optimized with HIP kernel)
        std::vector<std::vector<std::pair<uint32_t, float>>> results(numQueries);
        
        for (size_t q = 0; q < numQueries; q++) {
            std::vector<std::pair<uint32_t, float>> distPairs;
            distPairs.reserve(numVectors);
            
            for (size_t v = 0; v < numVectors; v++) {
                distPairs.emplace_back(v, distances[q * numVectors + v]);
            }
            
            std::partial_sort(distPairs.begin(), distPairs.begin() + k, distPairs.end(),
                [](const auto& a, const auto& b) { return a.second < b.second; });
            
            results[q].assign(distPairs.begin(), distPairs.begin() + k);
        }
        
        return results;
    }

private:
    bool initialized_ = false;
    int deviceId_ = 0;
    hipStream_t stream_ = nullptr;
};

} // namespace acceleration
} // namespace themis

#endif // THEMIS_ENABLE_HIP
