#include "plugins/plugin_manager.h"
#include "acceleration/plugin_security.h"
#include "utils/logger.h"
#include <filesystem>
#include <fstream>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace themis {
namespace plugins {

namespace fs = std::filesystem;

// ============================================================================
// Platform-specific DLL loading (reused from acceleration/plugin_loader.cpp)
// ============================================================================

void* PluginManager::loadLibrary(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void* PluginManager::getSymbol(void* handle, const std::string& symbolName) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbolName.c_str()));
#else
    return dlsym(handle, symbolName.c_str());
#endif
}

void PluginManager::unloadLibrary(void* handle) {
    if (!handle) return;
    
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

// ============================================================================
// Security & Hashing
// ============================================================================

std::string PluginManager::calculateFileHash(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return "";
    }
    
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer))) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }
    SHA256_Update(&sha256, buffer, file.gcount());
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str();
}

bool PluginManager::verifyPlugin(const std::string& path, std::string& error_message) {
    using namespace themis::acceleration;
    
    PluginSecurityPolicy policy;
    
#ifdef NDEBUG
    // Production: Require signature
    policy.requireSignature = true;
    policy.allowUnsigned = false;
#else
    // Development: Allow unsigned
    policy.requireSignature = false;
    policy.allowUnsigned = true;
#endif
    
    PluginSecurityVerifier verifier(policy);
    return verifier.verifyPlugin(path, error_message);
}

// ============================================================================
// Manifest Signature Verification
// ============================================================================

bool PluginManager::verifyManifestSignature(const std::string& manifest_path, std::string& error_message) {
    // Signature verification strategy:
    // 1. Check for manifest_path + ".sig" file (digital signature)
    // 2. Verify SHA256 hash matches signature file content
    // 3. In production, require valid signature
    
#ifdef NDEBUG
    // Production mode: Require signature
    std::string sig_path = manifest_path + ".sig";
    
    if (!fs::exists(sig_path)) {
        error_message = "Manifest signature file not found: " + sig_path;
        THEMIS_ERROR("{}", error_message);
        return false;
    }
    
    try {
        // Read signature file (contains expected SHA256 hash)
        std::ifstream sig_file(sig_path);
        std::string expected_hash;
        std::getline(sig_file, expected_hash);
        
        // Trim whitespace
        expected_hash.erase(0, expected_hash.find_first_not_of(" \t\n\r"));
        expected_hash.erase(expected_hash.find_last_not_of(" \t\n\r") + 1);
        
        // Compute actual hash of manifest
        std::string actual_hash = calculateFileHash(manifest_path);
        
        if (actual_hash.empty()) {
            error_message = "Failed to compute hash for manifest: " + manifest_path;
            THEMIS_ERROR("{}", error_message);
            return false;
        }
        
        // Compare hashes
        if (expected_hash != actual_hash) {
            error_message = "Manifest signature verification failed: hash mismatch\n"
                          "  Expected: " + expected_hash + "\n"
                          "  Actual:   " + actual_hash;
            THEMIS_ERROR("{}", error_message);
            return false;
        }
        
        THEMIS_INFO("Manifest signature verified: {}", manifest_path);
        return true;
        
    } catch (const std::exception& e) {
        error_message = std::string("Signature verification error: ") + e.what();
        THEMIS_ERROR("{}", error_message);
        return false;
    }
#else
    // Development mode: Signature optional, just warn if missing
    std::string sig_path = manifest_path + ".sig";
    
    if (!fs::exists(sig_path)) {
        THEMIS_WARN("Manifest signature file not found (development mode): {}", sig_path);
        return true;  // Allow in development
    }
    
    try {
        // Verify if signature exists
        std::ifstream sig_file(sig_path);
        std::string expected_hash;
        std::getline(sig_file, expected_hash);
        
        expected_hash.erase(0, expected_hash.find_first_not_of(" \t\n\r"));
        expected_hash.erase(expected_hash.find_last_not_of(" \t\n\r") + 1);
        
        std::string actual_hash = calculateFileHash(manifest_path);
        
        if (!actual_hash.empty() && expected_hash != actual_hash) {
            THEMIS_WARN("Manifest signature mismatch (development mode, allowing): {}", manifest_path);
            THEMIS_WARN("  Expected: {}", expected_hash);
            THEMIS_WARN("  Actual:   {}", actual_hash);
        } else {
            THEMIS_INFO("Manifest signature verified: {}", manifest_path);
        }
        
    } catch (const std::exception& e) {
        THEMIS_WARN("Signature verification warning (development mode): {}", e.what());
    }
    
    return true;  // Always allow in development
#endif
}

// ============================================================================
// Manifest Loading
// ============================================================================

std::optional<PluginManifest> PluginManager::loadManifest(const std::string& manifest_path) {
    if (!fs::exists(manifest_path)) {
        THEMIS_WARN("Plugin manifest not found: {}", manifest_path);
        return std::nullopt;
    }
    
    // Verify manifest signature before loading
    std::string error_message;
    if (!verifyManifestSignature(manifest_path, error_message)) {
        THEMIS_ERROR("Manifest signature verification failed for {}: {}", 
            manifest_path, error_message);
        return std::nullopt;
    }
    
    try {
        std::ifstream file(manifest_path);
        json j;
        file >> j;
        
        PluginManifest manifest;
        manifest.name = j.value("name", "");
        manifest.version = j.value("version", "1.0.0");
        manifest.description = j.value("description", "");
        
        // Parse type
        std::string type_str = j.value("type", "custom");
        if (type_str == "compute_backend") {
            manifest.type = PluginType::COMPUTE_BACKEND;
        } else if (type_str == "blob_storage") {
            manifest.type = PluginType::BLOB_STORAGE;
        } else if (type_str == "importer") {
            manifest.type = PluginType::IMPORTER;
        } else if (type_str == "exporter") {
            manifest.type = PluginType::EXPORTER;
        } else if (type_str == "hsm_provider") {
            manifest.type = PluginType::HSM_PROVIDER;
        } else if (type_str == "embedding") {
            manifest.type = PluginType::EMBEDDING;
        } else {
            manifest.type = PluginType::CUSTOM;
        }
        
        // Parse binaries
        if (j.contains("binary")) {
            auto& bin = j["binary"];
            manifest.binary_windows = bin.value("windows", "");
            manifest.binary_linux = bin.value("linux", "");
            manifest.binary_macos = bin.value("macos", "");
        }
        
        // Parse dependencies
        if (j.contains("dependencies") && j["dependencies"].is_array()) {
            for (const auto& dep : j["dependencies"]) {
                manifest.dependencies.push_back(dep.get<std::string>());
            }
        }
        
        // Parse capabilities
        if (j.contains("capabilities")) {
            auto& caps = j["capabilities"];
            manifest.capabilities.supports_streaming = caps.value("streaming", false);
            manifest.capabilities.supports_batching = caps.value("batching", false);
            manifest.capabilities.supports_transactions = caps.value("transactions", false);
            manifest.capabilities.thread_safe = caps.value("thread_safe", false);
            manifest.capabilities.gpu_accelerated = caps.value("gpu_accelerated", false);
        }
        
        manifest.auto_load = j.value("auto_load", false);
        manifest.load_priority = j.value("load_priority", 100);
        
        if (j.contains("config_schema")) {
            manifest.config_schema = j["config_schema"].dump();
        }
        
        return manifest;
        
    } catch (const std::exception& e) {
        THEMIS_ERROR("Failed to parse plugin manifest {}: {}", manifest_path, e.what());
        return std::nullopt;
    }
}

// ============================================================================
// Plugin Discovery & Loading
// ============================================================================

size_t PluginManager::scanPluginDirectory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        THEMIS_WARN("Plugin directory does not exist: {}", directory);
        return 0;
    }
    
    size_t discovered = 0;
    
    // Recursively scan for plugin.json files
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;
        
        std::string filename = entry.path().filename().string();
        if (filename == "plugin.json") {
            auto manifest = loadManifest(entry.path().string());
            if (!manifest) continue;
            
            // Determine binary path based on platform
            std::string binary_name;
#ifdef _WIN32
            binary_name = manifest->binary_windows;
#elif defined(__APPLE__)
            binary_name = manifest->binary_macos;
#else
            binary_name = manifest->binary_linux;
#endif
            
            if (binary_name.empty()) {
                THEMIS_WARN("No binary specified for current platform in manifest: {}", 
                    manifest->name);
                continue;
            }
            
            // Binary is in same directory as manifest
            fs::path binary_path = entry.path().parent_path() / binary_name;
            
            if (!fs::exists(binary_path)) {
                THEMIS_WARN("Plugin binary not found: {}", binary_path.string());
                continue;
            }
            
            // Register plugin
            PluginEntry plugin_entry;
            plugin_entry.name = manifest->name;
            plugin_entry.type = manifest->type;
            plugin_entry.path = binary_path.string();
            plugin_entry.manifest = *manifest;
            plugin_entry.loaded = false;
            
            plugins_[manifest->name] = std::move(plugin_entry);
            type_index_[manifest->type].push_back(manifest->name);
            
            discovered++;
            
            THEMIS_INFO("Discovered plugin: {} v{} ({})", 
                manifest->name, manifest->version, binary_path.string());
        }
    }
    
    THEMIS_INFO("Discovered {} plugins in {}", discovered, directory);
    return discovered;
}

IThemisPlugin* PluginManager::loadPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        THEMIS_ERROR("Plugin not found: {}", name);
        return nullptr;
    }
    
    auto& entry = it->second;
    
    // Already loaded?
    if (entry.loaded && entry.instance) {
        return entry.instance.get();
    }
    
    // Verify plugin
    std::string error_message;
    if (!verifyPlugin(entry.path, error_message)) {
        THEMIS_ERROR("Plugin verification failed for {}: {}", name, error_message);
        return nullptr;
    }
    
    // Load library
    void* handle = loadLibrary(entry.path);
    if (!handle) {
        THEMIS_ERROR("Failed to load plugin library: {}", entry.path);
#ifndef _WIN32
        THEMIS_ERROR("Error: {}", dlerror());
#endif
        return nullptr;
    }
    
    // Get factory function
    auto createFunc = reinterpret_cast<CreatePluginFunc>(getSymbol(handle, "createPlugin"));
    if (!createFunc) {
        THEMIS_ERROR("Plugin does not export createPlugin: {}", entry.path);
        unloadLibrary(handle);
        return nullptr;
    }
    
    // Create instance
    IThemisPlugin* plugin = createFunc();
    if (!plugin) {
        THEMIS_ERROR("Failed to create plugin instance: {}", name);
        unloadLibrary(handle);
        return nullptr;
    }
    
    // Initialize with empty config (can be configured later)
    if (!plugin->initialize("{}")) {
        THEMIS_ERROR("Failed to initialize plugin: {}", name);
        
        // Get destroy function and cleanup
        auto destroyFunc = reinterpret_cast<DestroyPluginFunc>(getSymbol(handle, "destroyPlugin"));
        if (destroyFunc) {
            destroyFunc(plugin);
        }
        unloadLibrary(handle);
        return nullptr;
    }
    
    // Store
    entry.library_handle = handle;
    entry.instance.reset(plugin);
    entry.loaded = true;
    entry.file_hash = calculateFileHash(entry.path);
    
    THEMIS_INFO("Loaded plugin: {} v{} (Hash: {}...)", 
        name, plugin->getVersion(), entry.file_hash.substr(0, 16));
    
    return plugin;
}

IThemisPlugin* PluginManager::loadPluginFromPath(
    const std::string& path,
    const std::string& config
) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Verify plugin
    std::string error_message;
    if (!verifyPlugin(path, error_message)) {
        THEMIS_ERROR("Plugin verification failed for {}: {}", path, error_message);
        return nullptr;
    }
    
    // Load library
    void* handle = loadLibrary(path);
    if (!handle) {
        THEMIS_ERROR("Failed to load plugin library: {}", path);
        return nullptr;
    }
    
    // Get factory function
    auto createFunc = reinterpret_cast<CreatePluginFunc>(getSymbol(handle, "createPlugin"));
    if (!createFunc) {
        THEMIS_ERROR("Plugin does not export createPlugin: {}", path);
        unloadLibrary(handle);
        return nullptr;
    }
    
    // Create instance
    IThemisPlugin* plugin = createFunc();
    if (!plugin) {
        THEMIS_ERROR("Failed to create plugin instance from: {}", path);
        unloadLibrary(handle);
        return nullptr;
    }
    
    // Initialize with provided config
    if (!plugin->initialize(config.c_str())) {
        THEMIS_ERROR("Failed to initialize plugin from: {}", path);
        
        auto destroyFunc = reinterpret_cast<DestroyPluginFunc>(getSymbol(handle, "destroyPlugin"));
        if (destroyFunc) {
            destroyFunc(plugin);
        }
        unloadLibrary(handle);
        return nullptr;
    }
    
    // Create entry for dynamically loaded plugin
    PluginEntry entry;
    entry.name = plugin->getName();
    entry.type = plugin->getType();
    entry.path = path;
    entry.library_handle = handle;
    entry.instance.reset(plugin);
    entry.loaded = true;
    entry.file_hash = calculateFileHash(path);
    
    // Store
    plugins_[entry.name] = std::move(entry);
    type_index_[plugin->getType()].push_back(plugin->getName());
    
    THEMIS_INFO("Dynamically loaded plugin: {} v{}", plugin->getName(), plugin->getVersion());
    
    return plugin;
}

void PluginManager::unloadPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = plugins_.find(name);
    if (it == plugins_.end() || !it->second.loaded) {
        return;
    }
    
    auto& entry = it->second;
    
    // Shutdown plugin
    if (entry.instance) {
        entry.instance->shutdown();
        
        // Get destroy function
        auto destroyFunc = reinterpret_cast<DestroyPluginFunc>(
            getSymbol(entry.library_handle, "destroyPlugin")
        );
        
        if (destroyFunc) {
            destroyFunc(entry.instance.release());
        } else {
            entry.instance.reset();
        }
    }
    
    // Unload library
    unloadLibrary(entry.library_handle);
    
    entry.library_handle = nullptr;
    entry.loaded = false;
    
    THEMIS_INFO("Unloaded plugin: {}", name);
}

void PluginManager::unloadAllPlugins() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& pair : plugins_) {
        if (!pair.second.loaded) continue;
        
        auto& entry = pair.second;
        
        if (entry.instance) {
            entry.instance->shutdown();
            
            auto destroyFunc = reinterpret_cast<DestroyPluginFunc>(
                getSymbol(entry.library_handle, "destroyPlugin")
            );
            
            if (destroyFunc) {
                destroyFunc(entry.instance.release());
            } else {
                entry.instance.reset();
            }
        }
        
        unloadLibrary(entry.library_handle);
        entry.library_handle = nullptr;
        entry.loaded = false;
    }
    
    THEMIS_INFO("Unloaded all plugins");
}

IThemisPlugin* PluginManager::getPlugin(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = plugins_.find(name);
    if (it != plugins_.end() && it->second.loaded) {
        return it->second.instance.get();
    }
    
    return nullptr;
}

std::vector<IThemisPlugin*> PluginManager::getPluginsByType(PluginType type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<IThemisPlugin*> result;
    
    auto it = type_index_.find(type);
    if (it != type_index_.end()) {
        for (const auto& name : it->second) {
            auto plugin_it = plugins_.find(name);
            if (plugin_it != plugins_.end() && plugin_it->second.loaded) {
                result.push_back(plugin_it->second.instance.get());
            }
        }
    }
    
    return result;
}

std::vector<PluginManifest> PluginManager::listPlugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PluginManifest> result;
    for (const auto& pair : plugins_) {
        result.push_back(pair.second.manifest);
    }
    
    return result;
}

std::vector<std::string> PluginManager::listLoadedPlugins() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> result;
    for (const auto& pair : plugins_) {
        if (pair.second.loaded) {
            result.push_back(pair.first);
        }
    }
    
    return result;
}

bool PluginManager::isPluginLoaded(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = plugins_.find(name);
    return it != plugins_.end() && it->second.loaded;
}

bool PluginManager::reloadPlugin(const std::string& name) {
    // Unload first
    unloadPlugin(name);
    
    // Then reload
    return loadPlugin(name) != nullptr;
}

size_t PluginManager::autoLoadPlugins() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Collect plugins to auto-load (sorted by priority)
    std::vector<std::pair<int, std::string>> to_load;
    
    for (const auto& pair : plugins_) {
        if (pair.second.manifest.auto_load && !pair.second.loaded) {
            to_load.push_back({pair.second.manifest.load_priority, pair.first});
        }
    }
    
    // Sort by priority (lower = higher priority)
    std::sort(to_load.begin(), to_load.end());
    
    size_t loaded = 0;
    for (const auto& [priority, name] : to_load) {
        // Temporarily unlock for loading
        mutex_.unlock();
        auto* plugin = loadPlugin(name);
        mutex_.lock();
        
        if (plugin) {
            loaded++;
        }
    }
    
    THEMIS_INFO("Auto-loaded {} plugins", loaded);
    return loaded;
}

std::optional<PluginManifest> PluginManager::getManifest(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = plugins_.find(name);
    if (it != plugins_.end()) {
        return it->second.manifest;
    }
    
    return std::nullopt;
}

PluginManager::~PluginManager() {
    unloadAllPlugins();
}

// Singleton
PluginManager& PluginManager::instance() {
    static PluginManager instance;
    return instance;
}

// ============================================================================
// Plugin Registry
// ============================================================================

void PluginRegistry::registerFactory(
    const std::string& name,
    PluginType type,
    PluginFactory factory
) {
    auto& registry = instance();
    std::lock_guard<std::mutex> lock(registry.mutex_);
    
    registry.factories_[name] = {type, factory};
    THEMIS_INFO("Registered plugin factory: {}", name);
}

std::unique_ptr<IThemisPlugin> PluginRegistry::createPlugin(const std::string& name) {
    auto& registry = instance();
    std::lock_guard<std::mutex> lock(registry.mutex_);
    
    auto it = registry.factories_.find(name);
    if (it != registry.factories_.end()) {
        return it->second.second();
    }
    
    return nullptr;
}

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry instance;
    return instance;
}

} // namespace plugins
} // namespace themis
