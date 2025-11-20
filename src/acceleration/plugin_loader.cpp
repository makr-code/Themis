#include "acceleration/plugin_loader.h"
#include <filesystem>
#include <iostream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace themis {
namespace acceleration {

PluginLoader::~PluginLoader() {
    unloadAllPlugins();
}

void* PluginLoader::loadLibrary(const std::string& path) {
#ifdef _WIN32
    return LoadLibraryA(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void* PluginLoader::getSymbol(void* handle, const std::string& symbolName) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbolName.c_str()));
#else
    return dlsym(handle, symbolName.c_str());
#endif
}

void PluginLoader::unloadLibrary(void* handle) {
    if (!handle) return;
    
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

bool PluginLoader::loadPlugin(const std::string& libraryPath) {
    // Load the shared library
    void* handle = loadLibrary(libraryPath);
    if (!handle) {
        std::cerr << "Failed to load plugin library: " << libraryPath << std::endl;
#ifndef _WIN32
        std::cerr << "Error: " << dlerror() << std::endl;
#endif
        return false;
    }
    
    // Get the plugin factory function
    auto createFunc = reinterpret_cast<CreatePluginFunc>(getSymbol(handle, "CreateBackendPlugin"));
    if (!createFunc) {
        std::cerr << "Plugin library does not export CreateBackendPlugin: " << libraryPath << std::endl;
        unloadLibrary(handle);
        return false;
    }
    
    // Create the plugin instance
    BackendPlugin* plugin = createFunc();
    if (!plugin) {
        std::cerr << "Failed to create plugin instance: " << libraryPath << std::endl;
        unloadLibrary(handle);
        return false;
    }
    
    // Store the plugin
    PluginHandle pluginHandle;
    pluginHandle.libraryHandle = handle;
    pluginHandle.plugin.reset(plugin);
    pluginHandle.name = plugin->pluginName();
    pluginHandle.path = libraryPath;
    
    plugins_.push_back(std::move(pluginHandle));
    
    std::cout << "Loaded plugin: " << plugin->pluginName() 
              << " v" << plugin->pluginVersion() << std::endl;
    
    return true;
}

size_t PluginLoader::loadPluginsFromDirectory(const std::string& directoryPath) {
    namespace fs = std::filesystem;
    
    if (!fs::exists(directoryPath) || !fs::is_directory(directoryPath)) {
        std::cerr << "Plugin directory does not exist: " << directoryPath << std::endl;
        return 0;
    }
    
    size_t loadedCount = 0;
    
    // Determine the platform-specific library extension
#ifdef _WIN32
    const std::string extension = ".dll";
#elif defined(__APPLE__)
    const std::string extension = ".dylib";
#else
    const std::string extension = ".so";
#endif
    
    // Scan directory for plugin libraries
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) continue;
        
        std::string path = entry.path().string();
        std::string filename = entry.path().filename().string();
        
        // Check if it's a plugin library (starts with "themis_accel_" and has correct extension)
        if (filename.find("themis_accel_") == 0 && 
            filename.find(extension) != std::string::npos) {
            
            if (loadPlugin(path)) {
                loadedCount++;
            }
        }
    }
    
    std::cout << "Loaded " << loadedCount << " acceleration plugins from " << directoryPath << std::endl;
    
    return loadedCount;
}

void PluginLoader::unloadPlugin(const std::string& pluginName) {
    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        if (it->name == pluginName) {
            std::cout << "Unloading plugin: " << pluginName << std::endl;
            unloadLibrary(it->libraryHandle);
            plugins_.erase(it);
            return;
        }
    }
}

void PluginLoader::unloadAllPlugins() {
    for (auto& plugin : plugins_) {
        std::cout << "Unloading plugin: " << plugin.name << std::endl;
        unloadLibrary(plugin.libraryHandle);
    }
    plugins_.clear();
}

BackendPlugin* PluginLoader::getPlugin(const std::string& pluginName) const {
    for (const auto& plugin : plugins_) {
        if (plugin.name == pluginName) {
            return plugin.plugin.get();
        }
    }
    return nullptr;
}

std::vector<BackendPlugin*> PluginLoader::getLoadedPlugins() const {
    std::vector<BackendPlugin*> result;
    result.reserve(plugins_.size());
    
    for (const auto& plugin : plugins_) {
        result.push_back(plugin.plugin.get());
    }
    
    return result;
}

} // namespace acceleration
} // namespace themis
