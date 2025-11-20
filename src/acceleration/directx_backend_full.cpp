// DirectX 12 Compute Shaders Backend (Windows only)
// Provides GPU acceleration using DirectX 12 Compute Shaders
// Native Windows GPU acceleration for NVIDIA, AMD, Intel GPUs

#include "acceleration/graphics_backends.h"

#ifdef THEMIS_ENABLE_DIRECTX

#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace themis {
namespace acceleration {

// ============================================================================
// DirectX 12 Helper Macros
// ============================================================================

#define DX_CHECK(call) \
    do { \
        HRESULT hr = call; \
        if (FAILED(hr)) { \
            std::cerr << "DirectX error in " << __FILE__ << ":" << __LINE__ \
                      << " - HRESULT: 0x" << std::hex << hr << std::endl; \
            return false; \
        } \
    } while(0)

#define DX_CHECK_THROW(call) \
    do { \
        HRESULT hr = call; \
        if (FAILED(hr)) { \
            throw std::runtime_error("DirectX error: HRESULT 0x" + std::to_string(hr)); \
        } \
    } while(0)

// ============================================================================
// DirectX 12 Compute Shader (HLSL)
// ============================================================================

const char* g_L2DistanceShader = R"HLSL(
// L2 Distance Compute Shader (HLSL)
RWStructuredBuffer<float> queries : register(u0);
RWStructuredBuffer<float> vectors : register(u1);
RWStructuredBuffer<float> distances : register(u2);

cbuffer Constants : register(b0)
{
    uint numQueries;
    uint numVectors;
    uint dim;
    uint padding;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint qIdx = DTid.y;
    uint vIdx = DTid.x;
    
    if (qIdx >= numQueries || vIdx >= numVectors)
        return;
    
    uint queryOffset = qIdx * dim;
    uint vectorOffset = vIdx * dim;
    
    float sum = 0.0f;
    
    [unroll(4)]
    for (uint i = 0; i < dim; i++)
    {
        float diff = queries[queryOffset + i] - vectors[vectorOffset + i];
        sum += diff * diff;
    }
    
    distances[qIdx * numVectors + vIdx] = sqrt(sum);
}
)HLSL";

const char* g_CosineDistanceShader = R"HLSL(
// Cosine Distance Compute Shader (HLSL)
RWStructuredBuffer<float> queries : register(u0);
RWStructuredBuffer<float> vectors : register(u1);
RWStructuredBuffer<float> distances : register(u2);

cbuffer Constants : register(b0)
{
    uint numQueries;
    uint numVectors;
    uint dim;
    uint padding;
};

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    uint qIdx = DTid.y;
    uint vIdx = DTid.x;
    
    if (qIdx >= numQueries || vIdx >= numVectors)
        return;
    
    uint queryOffset = qIdx * dim;
    uint vectorOffset = vIdx * dim;
    
    float dotProduct = 0.0f;
    float normQuery = 0.0f;
    float normVector = 0.0f;
    
    [unroll(4)]
    for (uint i = 0; i < dim; i++)
    {
        float q = queries[queryOffset + i];
        float v = vectors[vectorOffset + i];
        dotProduct += q * v;
        normQuery += q * q;
        normVector += v * v;
    }
    
    normQuery = sqrt(normQuery);
    normVector = sqrt(normVector);
    
    float cosineSim = (normQuery > 1e-10f && normVector > 1e-10f)
        ? dotProduct / (normQuery * normVector)
        : 0.0f;
    
    distances[qIdx * numVectors + vIdx] = 1.0f - cosineSim;
}
)HLSL";

// ============================================================================
// DirectX 12 Context
// ============================================================================

struct DirectXContext {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    
    // Compute pipeline objects
    ComPtr<ID3D12RootSignature> rootSignature;
    ComPtr<ID3D12PipelineState> l2Pipeline;
    ComPtr<ID3D12PipelineState> cosinePipeline;
    
    // Descriptor heap
    ComPtr<ID3D12DescriptorHeap> uavHeap;
    
    ~DirectXContext() {
        if (fenceEvent) CloseHandle(fenceEvent);
    }
};

// ============================================================================
// DirectXVectorBackend Implementation Extension
// ============================================================================

class DirectXVectorBackendImpl {
public:
    DirectXContext ctx;
    
    bool initialize() {
        // Enable debug layer in debug builds
#ifdef _DEBUG
        ComPtr<ID3D12Debug> debugController;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();
        }
#endif
        
        // Create DXGI factory
        ComPtr<IDXGIFactory6> factory;
        DX_CHECK(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));
        
        // Find best adapter (discrete GPU preferred)
        ComPtr<IDXGIAdapter1> adapter;
        UINT adapterIndex = 0;
        SIZE_T maxMemory = 0;
        
        while (factory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);
            
            // Skip software adapters
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapterIndex++;
                continue;
            }
            
            // Check if D3D12 is supported
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) {
                if (desc.DedicatedVideoMemory > maxMemory) {
                    maxMemory = desc.DedicatedVideoMemory;
                    // Keep this adapter
                    std::wcout << L"Found adapter: " << desc.Description 
                              << L" (" << (desc.DedicatedVideoMemory / (1024*1024)) << L" MB)" << std::endl;
                }
            }
            
            adapterIndex++;
        }
        
        // Create device
        DX_CHECK(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&ctx.device)));
        
        // Create command queue
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        DX_CHECK(ctx.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&ctx.commandQueue)));
        
        // Create command allocator and command list
        DX_CHECK(ctx.device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            IID_PPV_ARGS(&ctx.commandAllocator)
        ));
        
        DX_CHECK(ctx.device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_COMPUTE,
            ctx.commandAllocator.Get(),
            nullptr,
            IID_PPV_ARGS(&ctx.commandList)
        ));
        
        ctx.commandList->Close();
        
        // Create fence for synchronization
        DX_CHECK(ctx.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ctx.fence)));
        ctx.fenceValue = 1;
        
        ctx.fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!ctx.fenceEvent) {
            return false;
        }
        
        // Create descriptor heap for UAVs
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 10;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        DX_CHECK(ctx.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&ctx.uavHeap)));
        
        // Create root signature and pipelines
        if (!createRootSignature()) return false;
        if (!createComputePipelines()) return false;
        
        return true;
    }
    
    bool createRootSignature() {
        // Root parameters: 3 UAVs + 1 constant buffer
        D3D12_ROOT_PARAMETER rootParams[4] = {};
        
        // UAV 0: queries
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        rootParams[0].Descriptor.ShaderRegister = 0;
        rootParams[0].Descriptor.RegisterSpace = 0;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        // UAV 1: vectors
        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        rootParams[1].Descriptor.ShaderRegister = 1;
        rootParams[1].Descriptor.RegisterSpace = 0;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        // UAV 2: distances
        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        rootParams[2].Descriptor.ShaderRegister = 2;
        rootParams[2].Descriptor.RegisterSpace = 0;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        // Constants
        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParams[3].Constants.ShaderRegister = 0;
        rootParams[3].Constants.RegisterSpace = 0;
        rootParams[3].Constants.Num32BitValues = 4; // numQueries, numVectors, dim, padding
        rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        
        D3D12_ROOT_SIGNATURE_DESC rootSigDesc = {};
        rootSigDesc.NumParameters = 4;
        rootSigDesc.pParameters = rootParams;
        rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
        
        ComPtr<ID3DBlob> signature;
        ComPtr<ID3DBlob> error;
        
        HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
        if (FAILED(hr)) {
            if (error) {
                std::cerr << "Root signature serialization error: " 
                         << (char*)error->GetBufferPointer() << std::endl;
            }
            return false;
        }
        
        DX_CHECK(ctx.device->CreateRootSignature(
            0,
            signature->GetBufferPointer(),
            signature->GetBufferSize(),
            IID_PPV_ARGS(&ctx.rootSignature)
        ));
        
        return true;
    }
    
    bool createComputePipelines() {
        // Compile L2 shader
        ComPtr<ID3DBlob> l2ShaderBlob;
        ComPtr<ID3DBlob> errorBlob;
        
        HRESULT hr = D3DCompile(
            g_L2DistanceShader,
            strlen(g_L2DistanceShader),
            "L2Distance.hlsl",
            nullptr,
            nullptr,
            "CSMain",
            "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &l2ShaderBlob,
            &errorBlob
        );
        
        if (FAILED(hr)) {
            if (errorBlob) {
                std::cerr << "L2 shader compilation error: " 
                         << (char*)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }
        
        // Create L2 pipeline
        D3D12_COMPUTE_PIPELINE_STATE_DESC l2PipelineDesc = {};
        l2PipelineDesc.pRootSignature = ctx.rootSignature.Get();
        l2PipelineDesc.CS.pShaderBytecode = l2ShaderBlob->GetBufferPointer();
        l2PipelineDesc.CS.BytecodeLength = l2ShaderBlob->GetBufferSize();
        
        DX_CHECK(ctx.device->CreateComputePipelineState(&l2PipelineDesc, IID_PPV_ARGS(&ctx.l2Pipeline)));
        
        // Compile Cosine shader
        ComPtr<ID3DBlob> cosineShaderBlob;
        
        hr = D3DCompile(
            g_CosineDistanceShader,
            strlen(g_CosineDistanceShader),
            "CosineDistance.hlsl",
            nullptr,
            nullptr,
            "CSMain",
            "cs_5_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &cosineShaderBlob,
            &errorBlob
        );
        
        if (FAILED(hr)) {
            if (errorBlob) {
                std::cerr << "Cosine shader compilation error: " 
                         << (char*)errorBlob->GetBufferPointer() << std::endl;
            }
            return false;
        }
        
        // Create Cosine pipeline
        D3D12_COMPUTE_PIPELINE_STATE_DESC cosinePipelineDesc = {};
        cosinePipelineDesc.pRootSignature = ctx.rootSignature.Get();
        cosinePipelineDesc.CS.pShaderBytecode = cosineShaderBlob->GetBufferPointer();
        cosinePipelineDesc.CS.BytecodeLength = cosineShaderBlob->GetBufferSize();
        
        DX_CHECK(ctx.device->CreateComputePipelineState(&cosinePipelineDesc, IID_PPV_ARGS(&ctx.cosinePipeline)));
        
        return true;
    }
    
    ComPtr<ID3D12Resource> createBuffer(SIZE_T size, D3D12_HEAP_TYPE heapType) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = heapType;
        
        D3D12_RESOURCE_DESC resourceDesc = {};
        resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        resourceDesc.Width = size;
        resourceDesc.Height = 1;
        resourceDesc.DepthOrArraySize = 1;
        resourceDesc.MipLevels = 1;
        resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
        resourceDesc.SampleDesc.Count = 1;
        resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        
        ComPtr<ID3D12Resource> buffer;
        DX_CHECK_THROW(ctx.device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(&buffer)
        ));
        
        return buffer;
    }
    
    void waitForGPU() {
        const UINT64 fenceVal = ctx.fenceValue;
        ctx.commandQueue->Signal(ctx.fence.Get(), fenceVal);
        ctx.fenceValue++;
        
        if (ctx.fence->GetCompletedValue() < fenceVal) {
            ctx.fence->SetEventOnCompletion(fenceVal, ctx.fenceEvent);
            WaitForSingleObject(ctx.fenceEvent, INFINITE);
        }
    }
};

} // namespace acceleration
} // namespace themis

#endif // THEMIS_ENABLE_DIRECTX
