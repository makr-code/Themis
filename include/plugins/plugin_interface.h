#pragma once

#include <string>
#include <memory>
#include <functional>

/**
 * @brief Unified Plugin Interface for ThemisDB
 * 
 * This interface unifies existing plugin loaders:
 * - acceleration/plugin_loader.h (Compute backends)
 * - security/hsm_provider_pkcs11.cpp (PKCS#11 dynamic loading)
 * - acceleration/zluda_backend.cpp (ZLUDA dynamic loading)
 * 
 * Benefits:
 * - Single plugin architecture for all components
 * - Consistent security verification
 * - Unified plugin discovery and lifecycle
 * - Shared code for DLL loading (Windows/Linux/macOS)
 */

// Platform-specific export macros
#ifdef _WIN32
    #ifdef THEMIS_PLUGIN_EXPORTS
        #define THEMIS_PLUGIN_EXPORT __declspec(dllexport)
    #else
        #define THEMIS_PLUGIN_EXPORT __declspec(dllimport)
    #endif
#else
    #define THEMIS_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

namespace themis {
namespace plugins {

/**
 * @brief Plugin Type Categories
 * 
 * Maps to existing plugin systems:
 * - COMPUTE_BACKEND -> acceleration::BackendPlugin
 * - BLOB_STORAGE -> New blob storage backends
 * - IMPORTER -> New data importers
 * - HSM_PROVIDER -> security::HSMProvider (PKCS#11)
 */
enum class PluginType {
    COMPUTE_BACKEND,   // Vector/Graph/Geo acceleration (existing)
    BLOB_STORAGE,      // Storage backends (Filesystem, S3, Azure, WebDAV)
    IMPORTER,          // Data importers (PostgreSQL, MySQL, CSV)
    EXPORTER,          // Data exporters
    HSM_PROVIDER,      // Hardware Security Modules (PKCS#11)
    EMBEDDING,         // Embedding providers (Sentence-BERT, OpenAI)
    CUSTOM             // Custom plugins
};

/**
 * @brief Plugin Capabilities
 */
struct PluginCapabilities {
    bool supports_streaming = false;
    bool supports_batching = false;
    bool supports_transactions = false;
    bool thread_safe = false;
    bool gpu_accelerated = false;
};

/**
 * @brief Base Plugin Interface
 * 
 * All plugins must implement this interface.
 * Type-specific plugins should also implement their domain interface
 * (e.g., IBlobStorageBackend, IImporter, etc.)
 */
class IThemisPlugin {
public:
    virtual ~IThemisPlugin() = default;
    
    /**
     * @brief Get plugin name
     */
    virtual const char* getName() const = 0;
    
    /**
     * @brief Get plugin version (semantic versioning)
     */
    virtual const char* getVersion() const = 0;
    
    /**
     * @brief Get plugin type
     */
    virtual PluginType getType() const = 0;
    
    /**
     * @brief Get plugin capabilities
     */
    virtual PluginCapabilities getCapabilities() const = 0;
    
    /**
     * @brief Initialize plugin with configuration JSON
     * @param config_json Configuration as JSON string
     * @return true if initialized successfully
     */
    virtual bool initialize(const char* config_json) = 0;
    
    /**
     * @brief Shutdown plugin and release resources
     */
    virtual void shutdown() = 0;
    
    /**
     * @brief Get plugin instance (type-specific)
     * @return Pointer to plugin implementation (must be cast to specific type)
     * 
     * For COMPUTE_BACKEND: Cast to acceleration::BackendPlugin*
     * For BLOB_STORAGE: Cast to storage::IBlobStorageBackend*
     * For IMPORTER: Cast to importers::IImporter*
     */
    virtual void* getInstance() = 0;
};

/**
 * @brief Plugin Entry Points
 * 
 * Every plugin DLL must export these two functions:
 */
typedef IThemisPlugin* (*CreatePluginFunc)();
typedef void (*DestroyPluginFunc)(IThemisPlugin*);

/**
 * @brief Plugin Manifest (parsed from plugin.json)
 */
struct PluginManifest {
    std::string name;
    std::string version;
    std::string description;
    PluginType type;
    
    // Platform-specific binaries
    std::string binary_windows;  // .dll
    std::string binary_linux;    // .so
    std::string binary_macos;    // .dylib
    
    // Dependencies
    std::vector<std::string> dependencies;
    
    // Capabilities
    PluginCapabilities capabilities;
    
    // Auto-load on startup?
    bool auto_load = false;
    
    // Load priority (lower = higher priority)
    int load_priority = 100;
    
    // Config schema (JSON Schema)
    std::string config_schema;
};

} // namespace plugins
} // namespace themis

/**
 * @brief Convenience macro for plugin implementation
 * 
 * Usage:
 * ```cpp
 * class MyPlugin : public IThemisPlugin { ... };
 * 
 * THEMIS_PLUGIN_IMPL(MyPlugin)
 * ```
 */
#define THEMIS_PLUGIN_IMPL(PluginClass) \
    extern "C" { \
        THEMIS_PLUGIN_EXPORT themis::plugins::IThemisPlugin* createPlugin() { \
            return new PluginClass(); \
        } \
        THEMIS_PLUGIN_EXPORT void destroyPlugin(themis::plugins::IThemisPlugin* plugin) { \
            delete plugin; \
        } \
    }
