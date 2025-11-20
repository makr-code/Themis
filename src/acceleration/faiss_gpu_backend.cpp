#include "acceleration/faiss_gpu_backend.h"

#ifdef THEMIS_ENABLE_CUDA
#include <faiss/IndexFlat.h>
#include <faiss/IndexIVFFlat.h>
#include <faiss/IndexIVFPQ.h>
#include <faiss/gpu/GpuIndexFlat.h>
#include <faiss/gpu/GpuIndexIVFFlat.h>
#include <faiss/gpu/GpuIndexIVFPQ.h>
#include <faiss/gpu/StandardGpuResources.h>
#include <faiss/gpu/GpuCloner.h>
#include <faiss/index_io.h>
#include <cuda_runtime.h>
#endif

#include <iostream>
#include <stdexcept>
#include <cstring>

namespace themis {
namespace acceleration {

#ifdef THEMIS_ENABLE_CUDA

FaissGPUVectorBackend::FaissGPUVectorBackend() = default;

FaissGPUVectorBackend::~FaissGPUVectorBackend() {
    shutdown();
}

bool FaissGPUVectorBackend::isAvailable() const noexcept {
    int deviceCount = 0;
    cudaError_t err = cudaGetDeviceCount(&deviceCount);
    return (err == cudaSuccess && deviceCount > 0);
}

BackendCapabilities FaissGPUVectorBackend::getCapabilities() const {
    BackendCapabilities caps;
    caps.supportsVectorOps = true;
    caps.supportsGraphOps = false;
    caps.supportsGeoOps = false;
    caps.supportsBatchProcessing = true;
    caps.supportsAsync = true;
    
    if (isAvailable()) {
        cudaDeviceProp prop;
        if (cudaGetDeviceProperties(&prop, config_.deviceId) == cudaSuccess) {
            caps.deviceName = std::string(prop.name);
            caps.maxMemoryBytes = prop.totalGlobalMem;
            caps.computeUnits = prop.multiProcessorCount;
        }
    } else {
        caps.deviceName = "Faiss GPU (Not Available)";
    }
    
    return caps;
}

bool FaissGPUVectorBackend::initialize() {
    if (!isAvailable()) {
        std::cerr << "Faiss GPU: No CUDA device available" << std::endl;
        return false;
    }
    
    try {
        // Create GPU resources
        gpuResources_ = std::make_unique<faiss::gpu::StandardGpuResources>();
        
        // Set memory limit
        size_t memLimit = config_.maxMemoryMB * 1024 * 1024;
        gpuResources_->setTempMemory(memLimit);
        
        std::cout << "Faiss GPU Backend initialized successfully" << std::endl;
        std::cout << "  Device ID: " << config_.deviceId << std::endl;
        std::cout << "  Memory Limit: " << config_.maxMemoryMB << " MB" << std::endl;
        
        initialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Faiss GPU initialization failed: " << e.what() << std::endl;
        return false;
    }
}

void FaissGPUVectorBackend::shutdown() {
    if (initialized_) {
        destroyIndex();
        gpuResources_.reset();
        initialized_ = false;
    }
}

bool FaissGPUVectorBackend::initializeIndex(const Config& config) {
    if (!initialized_) {
        std::cerr << "Faiss GPU backend not initialized" << std::endl;
        return false;
    }
    
    config_ = config;
    
    try {
        destroyIndex();  // Clean up any existing index
        index_ = createIndex(config.indexType, config.dimension);
        currentIndexType_ = config.indexType;
        
        std::cout << "Faiss GPU index created:" << std::endl;
        std::cout << "  Type: " << static_cast<int>(config.indexType) << std::endl;
        std::cout << "  Dimension: " << config.dimension << std::endl;
        
        return index_ != nullptr;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to create Faiss index: " << e.what() << std::endl;
        return false;
    }
}

void* FaissGPUVectorBackend::createIndex(IndexType type, int dimension) {
    faiss::gpu::GpuIndexFlatConfig flatConfig;
    flatConfig.device = config_.deviceId;
    
    switch (type) {
        case IndexType::FLAT_L2: {
            auto* idx = new faiss::gpu::GpuIndexFlatL2(
                gpuResources_.get(),
                dimension,
                flatConfig
            );
            return static_cast<void*>(idx);
        }
        
        case IndexType::FLAT_IP: {
            auto* idx = new faiss::gpu::GpuIndexFlatIP(
                gpuResources_.get(),
                dimension,
                flatConfig
            );
            return static_cast<void*>(idx);
        }
        
        case IndexType::IVF_FLAT: {
            // Create quantizer on GPU
            auto* quantizer = new faiss::gpu::GpuIndexFlatL2(
                gpuResources_.get(),
                dimension,
                flatConfig
            );
            
            faiss::gpu::GpuIndexIVFFlatConfig ivfConfig;
            ivfConfig.device = config_.deviceId;
            
            auto* idx = new faiss::gpu::GpuIndexIVFFlat(
                gpuResources_.get(),
                dimension,
                config_.nlist,
                quantizer,
                faiss::METRIC_L2,
                ivfConfig
            );
            idx->nprobe = config_.nprobe;
            return static_cast<void*>(idx);
        }
        
        case IndexType::IVF_PQ: {
            // Create quantizer on GPU
            auto* quantizer = new faiss::gpu::GpuIndexFlatL2(
                gpuResources_.get(),
                dimension,
                flatConfig
            );
            
            faiss::gpu::GpuIndexIVFPQConfig ivfpqConfig;
            ivfpqConfig.device = config_.deviceId;
            
            auto* idx = new faiss::gpu::GpuIndexIVFPQ(
                gpuResources_.get(),
                dimension,
                config_.nlist,
                config_.m,
                config_.nbits,
                faiss::METRIC_L2,
                ivfpqConfig
            );
            idx->nprobe = config_.nprobe;
            return static_cast<void*>(idx);
        }
        
        default:
            throw std::runtime_error("Unknown index type");
    }
}

void FaissGPUVectorBackend::destroyIndex() {
    if (!index_) return;
    
    switch (currentIndexType_) {
        case IndexType::FLAT_L2:
            delete static_cast<faiss::gpu::GpuIndexFlatL2*>(index_);
            break;
        case IndexType::FLAT_IP:
            delete static_cast<faiss::gpu::GpuIndexFlatIP*>(index_);
            break;
        case IndexType::IVF_FLAT:
            delete static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
            break;
        case IndexType::IVF_PQ:
            delete static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
            break;
    }
    
    index_ = nullptr;
}

bool FaissGPUVectorBackend::trainIndex(const float* vectors, size_t numVectors) {
    if (!index_) {
        std::cerr << "Index not initialized" << std::endl;
        return false;
    }
    
    try {
        faiss::Index* idx = nullptr;
        
        switch (currentIndexType_) {
            case IndexType::FLAT_L2:
            case IndexType::FLAT_IP:
                // Flat indices don't need training
                return true;
                
            case IndexType::IVF_FLAT:
                idx = static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
                break;
                
            case IndexType::IVF_PQ:
                idx = static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
                break;
        }
        
        if (idx && !idx->is_trained) {
            std::cout << "Training Faiss index with " << numVectors << " vectors..." << std::endl;
            idx->train(numVectors, vectors);
            std::cout << "Training complete" << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Training failed: " << e.what() << std::endl;
        return false;
    }
}

bool FaissGPUVectorBackend::addVectors(const float* vectors, size_t numVectors) {
    if (!index_) {
        std::cerr << "Index not initialized" << std::endl;
        return false;
    }
    
    try {
        faiss::Index* idx = nullptr;
        
        switch (currentIndexType_) {
            case IndexType::FLAT_L2:
                idx = static_cast<faiss::gpu::GpuIndexFlatL2*>(index_);
                break;
            case IndexType::FLAT_IP:
                idx = static_cast<faiss::gpu::GpuIndexFlatIP*>(index_);
                break;
            case IndexType::IVF_FLAT:
                idx = static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
                break;
            case IndexType::IVF_PQ:
                idx = static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
                break;
        }
        
        if (!idx->is_trained) {
            std::cerr << "Index must be trained before adding vectors" << std::endl;
            return false;
        }
        
        idx->add(numVectors, vectors);
        std::cout << "Added " << numVectors << " vectors to index (total: " << idx->ntotal << ")" << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Adding vectors failed: " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::vector<std::pair<uint32_t, float>>> FaissGPUVectorBackend::search(
    const float* queries,
    size_t numQueries,
    size_t k
) {
    if (!index_) {
        std::cerr << "Index not initialized" << std::endl;
        return {};
    }
    
    try {
        faiss::Index* idx = nullptr;
        
        switch (currentIndexType_) {
            case IndexType::FLAT_L2:
                idx = static_cast<faiss::gpu::GpuIndexFlatL2*>(index_);
                break;
            case IndexType::FLAT_IP:
                idx = static_cast<faiss::gpu::GpuIndexFlatIP*>(index_);
                break;
            case IndexType::IVF_FLAT:
                idx = static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
                break;
            case IndexType::IVF_PQ:
                idx = static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
                break;
        }
        
        if (idx->ntotal == 0) {
            std::cerr << "Index is empty" << std::endl;
            return {};
        }
        
        // Allocate output buffers
        std::vector<float> distances(numQueries * k);
        std::vector<faiss::idx_t> labels(numQueries * k);
        
        // Perform search
        idx->search(numQueries, queries, k, distances.data(), labels.data());
        
        // Convert to output format
        std::vector<std::vector<std::pair<uint32_t, float>>> results(numQueries);
        for (size_t q = 0; q < numQueries; ++q) {
            results[q].reserve(k);
            for (size_t i = 0; i < k; ++i) {
                size_t idx_pos = q * k + i;
                if (labels[idx_pos] >= 0) {  // Valid result
                    results[q].emplace_back(
                        static_cast<uint32_t>(labels[idx_pos]),
                        distances[idx_pos]
                    );
                }
            }
        }
        
        return results;
        
    } catch (const std::exception& e) {
        std::cerr << "Search failed: " << e.what() << std::endl;
        return {};
    }
}

std::vector<float> FaissGPUVectorBackend::computeDistances(
    const float* queries,
    size_t numQueries,
    size_t dim,
    const float* vectors,
    size_t numVectors,
    bool useL2
) {
    // For one-time distance computation, create a temporary flat index
    Config tempConfig;
    tempConfig.indexType = useL2 ? IndexType::FLAT_L2 : IndexType::FLAT_IP;
    tempConfig.dimension = dim;
    tempConfig.deviceId = config_.deviceId;
    
    void* tempIndex = createIndex(tempConfig.indexType, dim);
    
    try {
        faiss::Index* idx = useL2 
            ? static_cast<faiss::Index*>(static_cast<faiss::gpu::GpuIndexFlatL2*>(tempIndex))
            : static_cast<faiss::Index*>(static_cast<faiss::gpu::GpuIndexFlatIP*>(tempIndex));
        
        // Add vectors to temporary index
        idx->add(numVectors, vectors);
        
        // Search for all vectors (k = numVectors)
        std::vector<float> distances(numQueries * numVectors);
        std::vector<faiss::idx_t> labels(numQueries * numVectors);
        
        idx->search(numQueries, queries, numVectors, distances.data(), labels.data());
        
        // Cleanup
        if (useL2) {
            delete static_cast<faiss::gpu::GpuIndexFlatL2*>(tempIndex);
        } else {
            delete static_cast<faiss::gpu::GpuIndexFlatIP*>(tempIndex);
        }
        
        return distances;
        
    } catch (const std::exception& e) {
        std::cerr << "Distance computation failed: " << e.what() << std::endl;
        
        // Cleanup on error
        if (useL2) {
            delete static_cast<faiss::gpu::GpuIndexFlatL2*>(tempIndex);
        } else {
            delete static_cast<faiss::gpu::GpuIndexFlatIP*>(tempIndex);
        }
        
        return {};
    }
}

std::vector<std::vector<std::pair<uint32_t, float>>> FaissGPUVectorBackend::batchKnnSearch(
    const float* queries,
    size_t numQueries,
    size_t dim,
    const float* vectors,
    size_t numVectors,
    size_t k,
    bool useL2
) {
    // For one-time KNN search, create a temporary flat index
    Config tempConfig;
    tempConfig.indexType = useL2 ? IndexType::FLAT_L2 : IndexType::FLAT_IP;
    tempConfig.dimension = dim;
    tempConfig.deviceId = config_.deviceId;
    
    void* tempIndex = createIndex(tempConfig.indexType, dim);
    
    try {
        faiss::Index* idx = useL2 
            ? static_cast<faiss::Index*>(static_cast<faiss::gpu::GpuIndexFlatL2*>(tempIndex))
            : static_cast<faiss::Index*>(static_cast<faiss::gpu::GpuIndexFlatIP*>(tempIndex));
        
        // Add vectors to temporary index
        idx->add(numVectors, vectors);
        
        // Allocate output buffers
        std::vector<float> distances(numQueries * k);
        std::vector<faiss::idx_t> labels(numQueries * k);
        
        // Perform search
        idx->search(numQueries, queries, k, distances.data(), labels.data());
        
        // Convert to output format
        std::vector<std::vector<std::pair<uint32_t, float>>> results(numQueries);
        for (size_t q = 0; q < numQueries; ++q) {
            results[q].reserve(k);
            for (size_t i = 0; i < k; ++i) {
                size_t idx_pos = q * k + i;
                if (labels[idx_pos] >= 0) {
                    results[q].emplace_back(
                        static_cast<uint32_t>(labels[idx_pos]),
                        distances[idx_pos]
                    );
                }
            }
        }
        
        // Cleanup
        if (useL2) {
            delete static_cast<faiss::gpu::GpuIndexFlatL2*>(tempIndex);
        } else {
            delete static_cast<faiss::gpu::GpuIndexFlatIP*>(tempIndex);
        }
        
        return results;
        
    } catch (const std::exception& e) {
        std::cerr << "Batch KNN search failed: " << e.what() << std::endl;
        
        // Cleanup on error
        if (useL2) {
            delete static_cast<faiss::gpu::GpuIndexFlatL2*>(tempIndex);
        } else {
            delete static_cast<faiss::gpu::GpuIndexFlatIP*>(tempIndex);
        }
        
        return {};
    }
}

bool FaissGPUVectorBackend::saveIndex(const std::string& filepath) {
    if (!index_) {
        std::cerr << "Index not initialized" << std::endl;
        return false;
    }
    
    try {
        // Get CPU index for saving
        faiss::Index* cpuIndex = nullptr;
        
        switch (currentIndexType_) {
            case IndexType::FLAT_L2: {
                auto* gpuIdx = static_cast<faiss::gpu::GpuIndexFlatL2*>(index_);
                cpuIndex = faiss::gpu::index_gpu_to_cpu(gpuIdx);
                break;
            }
            case IndexType::FLAT_IP: {
                auto* gpuIdx = static_cast<faiss::gpu::GpuIndexFlatIP*>(index_);
                cpuIndex = faiss::gpu::index_gpu_to_cpu(gpuIdx);
                break;
            }
            case IndexType::IVF_FLAT: {
                auto* gpuIdx = static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
                cpuIndex = faiss::gpu::index_gpu_to_cpu(gpuIdx);
                break;
            }
            case IndexType::IVF_PQ: {
                auto* gpuIdx = static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
                cpuIndex = faiss::gpu::index_gpu_to_cpu(gpuIdx);
                break;
            }
        }
        
        if (cpuIndex) {
            faiss::write_index(cpuIndex, filepath.c_str());
            delete cpuIndex;
            std::cout << "Index saved to: " << filepath << std::endl;
            return true;
        }
        
        return false;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to save index: " << e.what() << std::endl;
        return false;
    }
}

bool FaissGPUVectorBackend::loadIndex(const std::string& filepath) {
    try {
        // Load CPU index
        faiss::Index* cpuIndex = faiss::read_index(filepath.c_str());
        
        if (!cpuIndex) {
            std::cerr << "Failed to load index from: " << filepath << std::endl;
            return false;
        }
        
        // Transfer to GPU
        destroyIndex();
        
        faiss::gpu::GpuClonerOptions options;
        options.useFloat16 = false;  // Use full precision
        
        auto* gpuIndex = faiss::gpu::index_cpu_to_gpu(
            gpuResources_.get(),
            config_.deviceId,
            cpuIndex,
            &options
        );
        
        delete cpuIndex;
        
        if (!gpuIndex) {
            std::cerr << "Failed to transfer index to GPU" << std::endl;
            return false;
        }
        
        index_ = static_cast<void*>(gpuIndex);
        config_.dimension = gpuIndex->d;
        
        std::cout << "Index loaded from: " << filepath << std::endl;
        std::cout << "  Vectors: " << gpuIndex->ntotal << std::endl;
        std::cout << "  Dimension: " << gpuIndex->d << std::endl;
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to load index: " << e.what() << std::endl;
        return false;
    }
}

FaissGPUVectorBackend::IndexStats FaissGPUVectorBackend::getIndexStats() const {
    IndexStats stats;
    stats.type = currentIndexType_;
    
    if (!index_) {
        return stats;
    }
    
    try {
        faiss::Index* idx = nullptr;
        
        switch (currentIndexType_) {
            case IndexType::FLAT_L2:
                idx = static_cast<faiss::gpu::GpuIndexFlatL2*>(index_);
                break;
            case IndexType::FLAT_IP:
                idx = static_cast<faiss::gpu::GpuIndexFlatIP*>(index_);
                break;
            case IndexType::IVF_FLAT:
                idx = static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
                break;
            case IndexType::IVF_PQ:
                idx = static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
                break;
        }
        
        if (idx) {
            stats.numVectors = idx->ntotal;
            stats.dimension = idx->d;
            stats.isTrained = idx->is_trained;
            // Approximate memory usage (rough estimate)
            stats.memoryUsageBytes = stats.numVectors * stats.dimension * sizeof(float);
        }
        
    } catch (...) {
        // Return empty stats on error
    }
    
    return stats;
}

void FaissGPUVectorBackend::resetIndex() {
    if (index_) {
        try {
            faiss::Index* idx = nullptr;
            
            switch (currentIndexType_) {
                case IndexType::FLAT_L2:
                    idx = static_cast<faiss::gpu::GpuIndexFlatL2*>(index_);
                    break;
                case IndexType::FLAT_IP:
                    idx = static_cast<faiss::gpu::GpuIndexFlatIP*>(index_);
                    break;
                case IndexType::IVF_FLAT:
                    idx = static_cast<faiss::gpu::GpuIndexIVFFlat*>(index_);
                    break;
                case IndexType::IVF_PQ:
                    idx = static_cast<faiss::gpu::GpuIndexIVFPQ*>(index_);
                    break;
            }
            
            if (idx) {
                idx->reset();
                std::cout << "Index reset" << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Failed to reset index: " << e.what() << std::endl;
        }
    }
}

#endif // THEMIS_ENABLE_CUDA

} // namespace acceleration
} // namespace themis
