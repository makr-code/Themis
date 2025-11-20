// Full Vulkan Backend Implementation for ThemisDB
// Provides GPU-accelerated vector operations using Vulkan Compute Shaders
// Cross-platform support: Windows, Linux, macOS (via MoltenVK), Android

#include "acceleration/graphics_backends.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstring>
#include <dlfcn.h>  // For dynamic library loading (Unix)

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef THEMIS_ENABLE_VULKAN
#include <vulkan/vulkan.h>

namespace themis {
namespace acceleration {

// ============================================================================
// Vulkan Helper Structures
// ============================================================================

struct VulkanContext {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    uint32_t computeQueueFamilyIndex = 0;
    
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    
    // Compute pipelines
    VkPipeline l2Pipeline = VK_NULL_HANDLE;
    VkPipeline cosinePipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    
    // Shader modules
    VkShaderModule l2ShaderModule = VK_NULL_HANDLE;
    VkShaderModule cosineShaderModule = VK_NULL_HANDLE;
    
    // Device properties
    VkPhysicalDeviceProperties deviceProps;
    VkPhysicalDeviceMemoryProperties memoryProps;
};

struct VulkanBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

// ============================================================================
// Vulkan Helper Functions
// ============================================================================

static bool checkValidationLayerSupport(const std::vector<const char*>& layers) {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    
    for (const char* layerName : layers) {
        bool found = false;
        for (const auto& layerProps : availableLayers) {
            if (strcmp(layerName, layerProps.layerName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return true;
}

static uint32_t findMemoryType(const VkPhysicalDeviceMemoryProperties& memProps,
                                uint32_t typeFilter,
                                VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < memProps.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && 
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type");
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode = code.data();
    
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module");
    }
    
    return shaderModule;
}

static std::vector<uint32_t> compileGLSLtoSPIRV(const std::string& glslSource, 
                                                 const std::string& shaderType) {
    // In production, use glslangValidator or shaderc library to compile GLSL to SPIR-V
    // For this implementation, we assume pre-compiled SPIR-V binaries
    // or use runtime compilation with shaderc
    
    // Placeholder: would call shaderc_compile_into_spv() here
    std::cerr << "GLSL to SPIR-V compilation requires shaderc library" << std::endl;
    std::cerr << "Please pre-compile shaders with: glslangValidator -V shader.comp -o shader.spv" << std::endl;
    
    return {}; // Empty - requires actual compilation
}

static std::vector<uint32_t> loadSPIRV(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open SPIR-V file: " + filename);
    }
    
    size_t fileSize = (size_t)file.tellg();
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));
    
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), fileSize);
    file.close();
    
    return buffer;
}

// ============================================================================
// VulkanVectorBackend Implementation Extension
// ============================================================================

class VulkanVectorBackendImpl {
public:
    VulkanContext ctx;
    
    bool createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "ThemisDB";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "ThemisDB Acceleration";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_2;
        
        VkInstanceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
        // Enable validation layers in debug mode
#ifdef NDEBUG
        const bool enableValidation = false;
#else
        const bool enableValidation = true;
#endif
        
        std::vector<const char*> validationLayers;
        if (enableValidation) {
            validationLayers.push_back("VK_LAYER_KHRONOS_validation");
            if (!checkValidationLayerSupport(validationLayers)) {
                std::cout << "Validation layers requested but not available" << std::endl;
                validationLayers.clear();
            }
        }
        
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        
        if (vkCreateInstance(&createInfo, nullptr, &ctx.instance) != VK_SUCCESS) {
            return false;
        }
        
        return true;
    }
    
    bool selectPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, nullptr);
        
        if (deviceCount == 0) {
            std::cerr << "No Vulkan-capable devices found" << std::endl;
            return false;
        }
        
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(ctx.instance, &deviceCount, devices.data());
        
        // Select the first discrete GPU, or integrated if no discrete found
        VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
        for (const auto& device : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                selectedDevice = device;
                ctx.deviceProps = props;
                break;
            }
        }
        
        // Fallback to first device if no discrete GPU
        if (selectedDevice == VK_NULL_HANDLE) {
            selectedDevice = devices[0];
            vkGetPhysicalDeviceProperties(selectedDevice, &ctx.deviceProps);
        }
        
        ctx.physicalDevice = selectedDevice;
        vkGetPhysicalDeviceMemoryProperties(ctx.physicalDevice, &ctx.memoryProps);
        
        std::cout << "Selected Vulkan device: " << ctx.deviceProps.deviceName << std::endl;
        
        // Find compute queue family
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &queueFamilyCount, nullptr);
        
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(ctx.physicalDevice, &queueFamilyCount, queueFamilies.data());
        
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                ctx.computeQueueFamilyIndex = i;
                break;
            }
        }
        
        return true;
    }
    
    bool createLogicalDevice() {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = ctx.computeQueueFamilyIndex;
        queueCreateInfo.queueCount = 1;
        float queuePriority = 1.0f;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        
        VkPhysicalDeviceFeatures deviceFeatures{};
        
        VkDeviceCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.pQueueCreateInfos = &queueCreateInfo;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pEnabledFeatures = &deviceFeatures;
        
        if (vkCreateDevice(ctx.physicalDevice, &createInfo, nullptr, &ctx.device) != VK_SUCCESS) {
            return false;
        }
        
        vkGetDeviceQueue(ctx.device, ctx.computeQueueFamilyIndex, 0, &ctx.computeQueue);
        
        // Create command pool
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = ctx.computeQueueFamilyIndex;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        
        if (vkCreateCommandPool(ctx.device, &poolInfo, nullptr, &ctx.commandPool) != VK_SUCCESS) {
            return false;
        }
        
        // Create descriptor pool
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        poolSize.descriptorCount = 100; // Support up to 100 descriptor sets
        
        VkDescriptorPoolCreateInfo descPoolInfo{};
        descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descPoolInfo.poolSizeCount = 1;
        descPoolInfo.pPoolSizes = &poolSize;
        descPoolInfo.maxSets = 100;
        
        if (vkCreateDescriptorPool(ctx.device, &descPoolInfo, nullptr, &ctx.descriptorPool) != VK_SUCCESS) {
            return false;
        }
        
        return true;
    }
    
    bool createComputePipelines() {
        // Create descriptor set layout (3 storage buffers)
        VkDescriptorSetLayoutBinding bindings[3] = {};
        
        // Binding 0: Query vectors (readonly)
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        // Binding 1: Database vectors (readonly)
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        // Binding 2: Output distances (writeonly)
        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 3;
        layoutInfo.pBindings = bindings;
        
        if (vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &ctx.descriptorSetLayout) != VK_SUCCESS) {
            return false;
        }
        
        // Create pipeline layout with push constants
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(uint32_t) * 3; // numQueries, numVectors, dim
        
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &ctx.descriptorSetLayout;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        
        if (vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &ctx.pipelineLayout) != VK_SUCCESS) {
            return false;
        }
        
        // Load shader modules (SPIR-V)
        // In production, these would be pre-compiled or compiled at runtime
        try {
            // Try to load pre-compiled SPIR-V binaries
            auto l2SpirV = loadSPIRV("shaders/l2_distance.spv");
            auto cosineSpirV = loadSPIRV("shaders/cosine_distance.spv");
            
            ctx.l2ShaderModule = createShaderModule(ctx.device, l2SpirV);
            ctx.cosineShaderModule = createShaderModule(ctx.device, cosineSpirV);
        } catch (const std::exception& e) {
            std::cerr << "Failed to load SPIR-V shaders: " << e.what() << std::endl;
            std::cerr << "Please compile shaders with: glslangValidator -V shader.comp -o shader.spv" << std::endl;
            return false;
        }
        
        // Create compute pipeline for L2 distance
        VkPipelineShaderStageCreateInfo l2ShaderStage{};
        l2ShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        l2ShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        l2ShaderStage.module = ctx.l2ShaderModule;
        l2ShaderStage.pName = "main";
        
        VkComputePipelineCreateInfo l2PipelineInfo{};
        l2PipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        l2PipelineInfo.stage = l2ShaderStage;
        l2PipelineInfo.layout = ctx.pipelineLayout;
        
        if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &l2PipelineInfo, nullptr, &ctx.l2Pipeline) != VK_SUCCESS) {
            return false;
        }
        
        // Create compute pipeline for Cosine distance
        VkPipelineShaderStageCreateInfo cosineShaderStage{};
        cosineShaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cosineShaderStage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cosineShaderStage.module = ctx.cosineShaderModule;
        cosineShaderStage.pName = "main";
        
        VkComputePipelineCreateInfo cosinePipelineInfo{};
        cosinePipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cosinePipelineInfo.stage = cosineShaderStage;
        cosinePipelineInfo.layout = ctx.pipelineLayout;
        
        if (vkCreateComputePipelines(ctx.device, VK_NULL_HANDLE, 1, &cosinePipelineInfo, nullptr, &ctx.cosinePipeline) != VK_SUCCESS) {
            return false;
        }
        
        return true;
    }
    
    VulkanBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
        VulkanBuffer buffer;
        buffer.size = size;
        
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        
        if (vkCreateBuffer(ctx.device, &bufferInfo, nullptr, &buffer.buffer) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create buffer");
        }
        
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(ctx.device, buffer.buffer, &memRequirements);
        
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(ctx.memoryProps, memRequirements.memoryTypeBits, properties);
        
        if (vkAllocateMemory(ctx.device, &allocInfo, nullptr, &buffer.memory) != VK_SUCCESS) {
            vkDestroyBuffer(ctx.device, buffer.buffer, nullptr);
            throw std::runtime_error("Failed to allocate buffer memory");
        }
        
        vkBindBufferMemory(ctx.device, buffer.buffer, buffer.memory, 0);
        
        return buffer;
    }
    
    void destroyBuffer(VulkanBuffer& buffer) {
        if (buffer.mapped) {
            vkUnmapMemory(ctx.device, buffer.memory);
            buffer.mapped = nullptr;
        }
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(ctx.device, buffer.buffer, nullptr);
            buffer.buffer = VK_NULL_HANDLE;
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(ctx.device, buffer.memory, nullptr);
            buffer.memory = VK_NULL_HANDLE;
        }
    }
    
    void cleanup() {
        if (ctx.device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(ctx.device);
            
            if (ctx.l2Pipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(ctx.device, ctx.l2Pipeline, nullptr);
            }
            if (ctx.cosinePipeline != VK_NULL_HANDLE) {
                vkDestroyPipeline(ctx.device, ctx.cosinePipeline, nullptr);
            }
            if (ctx.pipelineLayout != VK_NULL_HANDLE) {
                vkDestroyPipelineLayout(ctx.device, ctx.pipelineLayout, nullptr);
            }
            if (ctx.descriptorSetLayout != VK_NULL_HANDLE) {
                vkDestroyDescriptorSetLayout(ctx.device, ctx.descriptorSetLayout, nullptr);
            }
            if (ctx.l2ShaderModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(ctx.device, ctx.l2ShaderModule, nullptr);
            }
            if (ctx.cosineShaderModule != VK_NULL_HANDLE) {
                vkDestroyShaderModule(ctx.device, ctx.cosineShaderModule, nullptr);
            }
            if (ctx.descriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(ctx.device, ctx.descriptorPool, nullptr);
            }
            if (ctx.commandPool != VK_NULL_HANDLE) {
                vkDestroyCommandPool(ctx.device, ctx.commandPool, nullptr);
            }
            
            vkDestroyDevice(ctx.device, nullptr);
        }
        
        if (ctx.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(ctx.instance, nullptr);
        }
    }
};

} // namespace acceleration
} // namespace themis

#endif // THEMIS_ENABLE_VULKAN
