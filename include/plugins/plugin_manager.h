#pragma once

#include "plugins/plugin_interface.h"
#include "acceleration/plugin_loader.h"  // Reuse existing loader
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace themis {
namespace plugins {

using json = nlohmann::json;

/**
 * @brief Unified Plugin Manager
 * 
 * Consolidates existing plugin systems:
 * 1. acceleration::PluginLoader (compute backends)
 * 2. HSM PKCS#11 dynamic loading
 * 3. ZLUDA dynamic loading
 * 
 * New capabilities:
 * - Plugin manifest (plugin.json) support
 * - Type-based plugin registry
 * - Auto-discovery from plugin directory
 * - Dependency resolution
 * - Hot-reload support
 * 
 * Thread-Safety: All methods are thread-safe
 */
class PluginManager {
private:
    struct PluginEntry {
        std::string name;
        PluginType type;
        std::string path;
        PluginManifest manifest;
        void* library_handle = nullptr;
        std::unique_ptr<IThemisPlugin> instance;
        bool loaded = false;
        std::string file_hash;
    };
    
    std::unordered_map<std::string, PluginEntry> plugins_;  // name -> entry
    std::unordered_map<PluginType, std::vector<std::string>> type_index_;  // type -> plugin names
    mutable std::mutex mutex_;
    
    // Reuse existing platform-specific loading from acceleration/plugin_loader.cpp
    void* loadLibrary(const std::string& path);
    void* getSymbol(void* handle, const std::string& symbolName);
    void unloadLibrary(void* handle);
    
    // Manifest loading
    std::optional<PluginManifest> loadManifest(const std::string& manifest_path);
    
    // Security verification (reuse acceleration/plugin_security.h)
    bool verifyPlugin(const std::string& path, std::string& error_message);
    
    std::string calculateFileHash(const std::string& path);
    
public:
    PluginManager() = default;
    ~PluginManager();
    
    // Prevent copying
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    
    /**
     * @brief Scan plugin directory for manifests
     * @param directory Path to plugin directory
     * @return Number of plugins discovered
     */
    size_t scanPluginDirectory(const std::string& directory);
    
    /**
     * @brief Load a plugin by name
     * @param name Plugin name (from manifest)
     * @return Loaded plugin instance or nullptr
     */
    IThemisPlugin* loadPlugin(const std::string& name);
    
    /**
     * @brief Load a plugin from explicit path
     * @param path Path to plugin DLL/SO
     * @param config Optional configuration JSON
     * @return Loaded plugin instance or nullptr
     */
    IThemisPlugin* loadPluginFromPath(
        const std::string& path,
        const std::string& config = "{}"
    );
    
    /**
     * @brief Unload a plugin
     * @param name Plugin name
     */
    void unloadPlugin(const std::string& name);
    
    /**
     * @brief Unload all plugins
     */
    void unloadAllPlugins();
    
    /**
     * @brief Get loaded plugin by name
     * @param name Plugin name
     * @return Plugin instance or nullptr if not loaded
     */
    IThemisPlugin* getPlugin(const std::string& name) const;
    
    /**
     * @brief Get all plugins of a specific type
     * @param type Plugin type
     * @return Vector of plugin instances
     */
    std::vector<IThemisPlugin*> getPluginsByType(PluginType type) const;
    
    /**
     * @brief List all discovered plugins (loaded or not)
     * @return Vector of plugin manifests
     */
    std::vector<PluginManifest> listPlugins() const;
    
    /**
     * @brief List loaded plugins
     * @return Vector of plugin names
     */
    std::vector<std::string> listLoadedPlugins() const;
    
    /**
     * @brief Check if plugin is loaded
     * @param name Plugin name
     * @return true if loaded
     */
    bool isPluginLoaded(const std::string& name) const;
    
    /**
     * @brief Reload a plugin (hot-reload)
     * @param name Plugin name
     * @return true if successful
     */
    bool reloadPlugin(const std::string& name);
    
    /**
     * @brief Auto-load plugins marked with auto_load=true
     * @return Number of plugins loaded
     */
    size_t autoLoadPlugins();
    
    /**
     * @brief Get plugin manifest
     * @param name Plugin name
     * @return Manifest or nullopt if not found
     */
    std::optional<PluginManifest> getManifest(const std::string& name) const;
    
    /**
     * @brief Singleton instance
     */
    static PluginManager& instance();
};

/**
 * @brief Plugin Registry
 * 
 * Global registry for type-specific plugin factories.
 * Allows third-party code to register plugin types.
 */
class PluginRegistry {
public:
    using PluginFactory = std::function<std::unique_ptr<IThemisPlugin>()>;
    
    /**
     * @brief Register a plugin factory
     * @param name Plugin name
     * @param type Plugin type
     * @param factory Factory function
     */
    static void registerFactory(
        const std::string& name,
        PluginType type,
        PluginFactory factory
    );
    
    /**
     * @brief Create plugin from factory
     * @param name Plugin name
     * @return Plugin instance or nullptr
     */
    static std::unique_ptr<IThemisPlugin> createPlugin(const std::string& name);
    
    /**
     * @brief Get singleton instance
     */
    static PluginRegistry& instance();
    
private:
    std::unordered_map<std::string, std::pair<PluginType, PluginFactory>> factories_;
    mutable std::mutex mutex_;
};

/**
 * @brief Helper class for automatic plugin registration
 * 
 * Usage:
 * ```cpp
 * // In plugin implementation
 * static PluginRegistrar<MyPlugin> registrar("my_plugin", PluginType::BLOB_STORAGE);
 * ```
 */
template<typename PluginClass>
class PluginRegistrar {
public:
    PluginRegistrar(const std::string& name, PluginType type) {
        PluginRegistry::registerFactory(
            name,
            type,
            []() { return std::make_unique<PluginClass>(); }
        );
    }
};

} // namespace plugins
} // namespace themis
