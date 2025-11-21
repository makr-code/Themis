// Test: Plugin Lifecycle Management
// Tests plugin loading, unloading, hot reload, and dependency management

#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

// Mock plugin system
class PluginManager {
public:
    enum class PluginState {
        UNLOADED,
        LOADING,
        LOADED,
        RUNNING,
        STOPPING,
        FAILED
    };
    
    struct PluginInfo {
        std::string name;
        std::string version;
        std::vector<std::string> dependencies;
        PluginState state;
        void* handle;
    };
    
    struct PluginMetadata {
        std::string name;
        std::string version;
        std::string api_version;
        std::vector<std::string> dependencies;
    };
    
    bool load_plugin(const std::string& name, const PluginMetadata& metadata) {
        if (plugins_.find(name) != plugins_.end() && 
            plugins_[name].state != PluginState::UNLOADED) {
            return false;  // Already loaded
        }
        
        // Check dependencies
        for (const auto& dep : metadata.dependencies) {
            if (plugins_.find(dep) == plugins_.end() || 
                plugins_[dep].state != PluginState::LOADED) {
                return false;  // Missing dependency
            }
        }
        
        // Check for circular dependencies
        if (has_circular_dependency(name, metadata.dependencies)) {
            return false;
        }
        
        // Check API version compatibility
        if (!is_api_compatible(metadata.api_version)) {
            return false;
        }
        
        PluginInfo info;
        info.name = name;
        info.version = metadata.version;
        info.dependencies = metadata.dependencies;
        info.state = PluginState::LOADED;
        info.handle = reinterpret_cast<void*>(0x1000);  // Mock handle
        
        plugins_[name] = info;
        load_order_.push_back(name);
        return true;
    }
    
    bool unload_plugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) {
            return false;
        }
        
        // Check if other plugins depend on this one
        for (const auto& [plugin_name, plugin_info] : plugins_) {
            if (plugin_name != name) {
                for (const auto& dep : plugin_info.dependencies) {
                    if (dep == name && plugin_info.state != PluginState::UNLOADED) {
                        return false;  // Cannot unload, dependency exists
                    }
                }
            }
        }
        
        it->second.state = PluginState::UNLOADED;
        it->second.handle = nullptr;
        
        // Remove from load order
        load_order_.erase(
            std::remove(load_order_.begin(), load_order_.end(), name),
            load_order_.end()
        );
        
        return true;
    }
    
    bool hot_reload_plugin(const std::string& name) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) {
            return false;
        }
        
        // Save dependencies
        auto deps = it->second.dependencies;
        auto version = it->second.version;
        
        // Unload
        it->second.state = PluginState::STOPPING;
        it->second.handle = nullptr;
        
        // Reload
        it->second.state = PluginState::LOADING;
        it->second.handle = reinterpret_cast<void*>(0x2000);  // New handle
        it->second.state = PluginState::LOADED;
        
        return true;
    }
    
    PluginState get_plugin_state(const std::string& name) const {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) {
            return PluginState::UNLOADED;
        }
        return it->second.state;
    }
    
    std::vector<std::string> get_load_order() const {
        return load_order_;
    }
    
    bool save_plugin_state(const std::string& name, const std::string& state_data) {
        plugin_states_[name] = state_data;
        return true;
    }
    
    std::string load_plugin_state(const std::string& name) const {
        auto it = plugin_states_.find(name);
        if (it == plugin_states_.end()) {
            return "";
        }
        return it->second;
    }
    
private:
    std::map<std::string, PluginInfo> plugins_;
    std::vector<std::string> load_order_;
    std::map<std::string, std::string> plugin_states_;
    std::string current_api_version_ = "1.0.0";
    
    bool has_circular_dependency(const std::string& name, 
                                 const std::vector<std::string>& deps) {
        for (const auto& dep : deps) {
            if (dep == name) {
                return true;  // Direct circular dependency
            }
            
            auto it = plugins_.find(dep);
            if (it != plugins_.end()) {
                for (const auto& transitive_dep : it->second.dependencies) {
                    if (transitive_dep == name) {
                        return true;  // Transitive circular dependency
                    }
                }
            }
        }
        return false;
    }
    
    bool is_api_compatible(const std::string& plugin_api_version) {
        // Simple version check: major version must match
        size_t dot1 = current_api_version_.find('.');
        size_t dot2 = plugin_api_version.find('.');
        
        if (dot1 == std::string::npos || dot2 == std::string::npos) {
            return false;
        }
        
        std::string current_major = current_api_version_.substr(0, dot1);
        std::string plugin_major = plugin_api_version.substr(0, dot2);
        
        return current_major == plugin_major;
    }
};

// Test: Basic plugin loading
TEST(PluginLifecycleTest, BasicLoading) {
    PluginManager manager;
    
    PluginManager::PluginMetadata metadata;
    metadata.name = "test_plugin";
    metadata.version = "1.0.0";
    metadata.api_version = "1.0.0";
    
    EXPECT_TRUE(manager.load_plugin("test_plugin", metadata));
    EXPECT_EQ(manager.get_plugin_state("test_plugin"), PluginManager::PluginState::LOADED);
}

// Test: Plugin unloading
TEST(PluginLifecycleTest, BasicUnloading) {
    PluginManager manager;
    
    PluginManager::PluginMetadata metadata;
    metadata.name = "test_plugin";
    metadata.version = "1.0.0";
    metadata.api_version = "1.0.0";
    
    EXPECT_TRUE(manager.load_plugin("test_plugin", metadata));
    EXPECT_TRUE(manager.unload_plugin("test_plugin"));
    EXPECT_EQ(manager.get_plugin_state("test_plugin"), PluginManager::PluginState::UNLOADED);
}

// Test: Hot reload
TEST(PluginLifecycleTest, HotReload) {
    PluginManager manager;
    
    PluginManager::PluginMetadata metadata;
    metadata.name = "test_plugin";
    metadata.version = "1.0.0";
    metadata.api_version = "1.0.0";
    
    EXPECT_TRUE(manager.load_plugin("test_plugin", metadata));
    EXPECT_TRUE(manager.hot_reload_plugin("test_plugin"));
    EXPECT_EQ(manager.get_plugin_state("test_plugin"), PluginManager::PluginState::LOADED);
}

// Test: Dependency resolution
TEST(PluginLifecycleTest, DependencyResolution) {
    PluginManager manager;
    
    // Load base plugin
    PluginManager::PluginMetadata base_metadata;
    base_metadata.name = "base_plugin";
    base_metadata.version = "1.0.0";
    base_metadata.api_version = "1.0.0";
    EXPECT_TRUE(manager.load_plugin("base_plugin", base_metadata));
    
    // Load dependent plugin
    PluginManager::PluginMetadata dep_metadata;
    dep_metadata.name = "dependent_plugin";
    dep_metadata.version = "1.0.0";
    dep_metadata.api_version = "1.0.0";
    dep_metadata.dependencies = {"base_plugin"};
    EXPECT_TRUE(manager.load_plugin("dependent_plugin", dep_metadata));
    
    // Should not be able to unload base while dependent is loaded
    EXPECT_FALSE(manager.unload_plugin("base_plugin"));
    
    // Unload dependent first, then base should work
    EXPECT_TRUE(manager.unload_plugin("dependent_plugin"));
    EXPECT_TRUE(manager.unload_plugin("base_plugin"));
}

// Test: Circular dependency detection
TEST(PluginLifecycleTest, CircularDependencyDetection) {
    PluginManager manager;
    
    // Load plugin A
    PluginManager::PluginMetadata metadata_a;
    metadata_a.name = "plugin_a";
    metadata_a.version = "1.0.0";
    metadata_a.api_version = "1.0.0";
    EXPECT_TRUE(manager.load_plugin("plugin_a", metadata_a));
    
    // Try to load plugin B that depends on A and A depends on B (circular)
    PluginManager::PluginMetadata metadata_b;
    metadata_b.name = "plugin_b";
    metadata_b.version = "1.0.0";
    metadata_b.api_version = "1.0.0";
    metadata_b.dependencies = {"plugin_a"};
    EXPECT_TRUE(manager.load_plugin("plugin_b", metadata_b));
    
    // Now try to add circular dependency (should fail in real scenario)
    PluginManager::PluginMetadata metadata_c;
    metadata_c.name = "plugin_c";
    metadata_c.version = "1.0.0";
    metadata_c.api_version = "1.0.0";
    metadata_c.dependencies = {"plugin_c"};  // Self-dependency
    EXPECT_FALSE(manager.load_plugin("plugin_c", metadata_c));
}

// Test: Version compatibility
TEST(PluginLifecycleTest, VersionCompatibility) {
    PluginManager manager;
    
    // Compatible version (same major)
    PluginManager::PluginMetadata compatible;
    compatible.name = "compatible_plugin";
    compatible.version = "1.5.0";
    compatible.api_version = "1.5.0";
    EXPECT_TRUE(manager.load_plugin("compatible_plugin", compatible));
    
    // Incompatible version (different major)
    PluginManager::PluginMetadata incompatible;
    incompatible.name = "incompatible_plugin";
    incompatible.version = "2.0.0";
    incompatible.api_version = "2.0.0";
    EXPECT_FALSE(manager.load_plugin("incompatible_plugin", incompatible));
}

// Test: Plugin state persistence
TEST(PluginLifecycleTest, StatePersistence) {
    PluginManager manager;
    
    PluginManager::PluginMetadata metadata;
    metadata.name = "stateful_plugin";
    metadata.version = "1.0.0";
    metadata.api_version = "1.0.0";
    
    EXPECT_TRUE(manager.load_plugin("stateful_plugin", metadata));
    
    // Save state
    std::string state_data = "plugin_state_data";
    EXPECT_TRUE(manager.save_plugin_state("stateful_plugin", state_data));
    
    // Load state
    std::string loaded_state = manager.load_plugin_state("stateful_plugin");
    EXPECT_EQ(loaded_state, state_data);
}

// Test: Error handling during lifecycle events
TEST(PluginLifecycleTest, ErrorHandling) {
    PluginManager manager;
    
    // Try to unload non-existent plugin
    EXPECT_FALSE(manager.unload_plugin("non_existent"));
    
    // Try to hot reload non-existent plugin
    EXPECT_FALSE(manager.hot_reload_plugin("non_existent"));
    
    // Try to load plugin with missing dependency
    PluginManager::PluginMetadata metadata;
    metadata.name = "plugin_with_missing_dep";
    metadata.version = "1.0.0";
    metadata.api_version = "1.0.0";
    metadata.dependencies = {"missing_plugin"};
    EXPECT_FALSE(manager.load_plugin("plugin_with_missing_dep", metadata));
}

// Test: Load order tracking
TEST(PluginLifecycleTest, LoadOrderTracking) {
    PluginManager manager;
    
    PluginManager::PluginMetadata metadata1;
    metadata1.name = "plugin1";
    metadata1.version = "1.0.0";
    metadata1.api_version = "1.0.0";
    manager.load_plugin("plugin1", metadata1);
    
    PluginManager::PluginMetadata metadata2;
    metadata2.name = "plugin2";
    metadata2.version = "1.0.0";
    metadata2.api_version = "1.0.0";
    manager.load_plugin("plugin2", metadata2);
    
    auto load_order = manager.get_load_order();
    EXPECT_EQ(load_order.size(), 2);
    EXPECT_EQ(load_order[0], "plugin1");
    EXPECT_EQ(load_order[1], "plugin2");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
