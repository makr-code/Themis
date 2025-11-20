#pragma once

#include "acceleration/compute_backend.h"

namespace themis {
namespace acceleration {

// DirectX 12 Compute Shaders backend (Windows only)
class DirectXVectorBackend : public IVectorBackend {
public:
    DirectXVectorBackend() = default;
    ~DirectXVectorBackend() override;
    
    const char* name() const noexcept override { return "DirectX"; }
    BackendType type() const noexcept override { return BackendType::DIRECTX; }
    bool isAvailable() const noexcept override;
    
    BackendCapabilities getCapabilities() const override;
    bool initialize() override;
    void shutdown() override;
    
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
    void* device_ = nullptr;  // ID3D12Device*
    void* commandQueue_ = nullptr;  // ID3D12CommandQueue*
};

// Vulkan Compute backend (cross-platform)
class VulkanVectorBackend : public IVectorBackend {
public:
    VulkanVectorBackend() = default;
    ~VulkanVectorBackend() override;
    
    const char* name() const noexcept override { return "Vulkan"; }
    BackendType type() const noexcept override { return BackendType::VULKAN; }
    bool isAvailable() const noexcept override;
    
    BackendCapabilities getCapabilities() const override;
    bool initialize() override;
    void shutdown() override;
    
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
    void* instance_ = nullptr;  // VkInstance
    void* device_ = nullptr;    // VkDevice
    void* queue_ = nullptr;     // VkQueue
};

// OpenGL Compute Shaders backend (legacy support)
class OpenGLVectorBackend : public IVectorBackend {
public:
    OpenGLVectorBackend() = default;
    ~OpenGLVectorBackend() override;
    
    const char* name() const noexcept override { return "OpenGL"; }
    BackendType type() const noexcept override { return BackendType::OPENGL; }
    bool isAvailable() const noexcept override;
    
    BackendCapabilities getCapabilities() const override;
    bool initialize() override;
    void shutdown() override;
    
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
    void* context_ = nullptr;  // OpenGL context
};

} // namespace acceleration
} // namespace themis
