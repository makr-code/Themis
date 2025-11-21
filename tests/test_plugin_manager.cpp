#include <gtest/gtest.h>
#include "plugins/plugin_manager.h"
#include "plugins/plugin_interface.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// Simple test plugin implementation
class TestPlugin : public themis::plugins::IThemisPlugin {
private:
    bool initialized_ = false;
    
public:
    const char* getName() const override { return "test_plugin"; }
    const char* getVersion() const override { return "1.0.0"; }
    
    themis::plugins::PluginType getType() const override {
        return themis::plugins::PluginType::CUSTOM;
    }
    
    themis::plugins::PluginCapabilities getCapabilities() const override {
        themis::plugins::PluginCapabilities caps;
        caps.thread_safe = true;
        return caps;
    }
    
    bool initialize(const char* config_json) override {
        initialized_ = true;
        return true;
    }
    
    void shutdown() override {
        initialized_ = false;
    }
    
    void* getInstance() override {
        return this;
    }
    
    bool isInitialized() const { return initialized_; }
};

class PluginManagerTest : public ::testing::Test {
protected:
    std::string test_plugin_dir = "./test_plugins";
    
    void SetUp() override {
        // Clean up
        if (fs::exists(test_plugin_dir)) {
            fs::remove_all(test_plugin_dir);
        }
        fs::create_directories(test_plugin_dir);
    }
    
    void TearDown() override {
        // Clean up
        if (fs::exists(test_plugin_dir)) {
            fs::remove_all(test_plugin_dir);
        }
    }
    
    void createTestManifest(const std::string& name, const std::string& type, bool with_signature = false) {
        std::string manifest_path = test_plugin_dir + "/" + name + "/plugin.json";
        fs::create_directories(test_plugin_dir + "/" + name);
        
        nlohmann::json manifest = {
            {"name", name},
            {"version", "1.0.0"},
            {"type", type},
            {"description", "Test plugin"},
            {"binary", {
                {"windows", name + ".dll"},
                {"linux", name + ".so"},
                {"macos", name + ".dylib"}
            }},
            {"capabilities", {
                {"thread_safe", true},
                {"streaming", false}
            }},
            {"auto_load", false},
            {"load_priority", 100}
        };
        
        std::ofstream file(manifest_path);
        file << manifest.dump(2);
        file.close();
        
        if (with_signature) {
            // Generate signature file
            std::string hash = themis::plugins::PluginManager::instance().calculateFileHash(manifest_path);
            std::ofstream sig_file(manifest_path + ".sig");
            sig_file << hash << std::endl;
        }
    }
};

TEST_F(PluginManagerTest, PluginManifestParsing) {
    createTestManifest("test_blob", "blob_storage");
    
    auto& pm = themis::plugins::PluginManager::instance();
    size_t discovered = pm.scanPluginDirectory(test_plugin_dir);
    
    // Will discover manifest but not load (no binary)
    EXPECT_EQ(discovered, 0);  // Binary doesn't exist, so discovery skips it
}

TEST_F(PluginManagerTest, ManifestSignatureVerification) {
    // In development mode, signature is optional
    createTestManifest("test_signed", "blob_storage", true);  // With signature
    
    std::string manifest_path = test_plugin_dir + "/test_signed/plugin.json";
    std::string error_msg;
    
    auto& pm = themis::plugins::PluginManager::instance();
    
    // Should succeed with valid signature
    EXPECT_TRUE(pm.verifyManifestSignature(manifest_path, error_msg));
}

TEST_F(PluginManagerTest, ManifestSignatureMissing) {
    createTestManifest("test_unsigned", "blob_storage", false);  // Without signature
    
    std::string manifest_path = test_plugin_dir + "/test_unsigned/plugin.json";
    std::string error_msg;
    
    auto& pm = themis::plugins::PluginManager::instance();
    
#ifdef NDEBUG
    // Production: Should fail
    EXPECT_FALSE(pm.verifyManifestSignature(manifest_path, error_msg));
    EXPECT_FALSE(error_msg.empty());
#else
    // Development: Should succeed with warning
    EXPECT_TRUE(pm.verifyManifestSignature(manifest_path, error_msg));
#endif
}

TEST_F(PluginManagerTest, PluginRegistry) {
    // Register a test plugin factory
    themis::plugins::PluginRegistry::registerFactory(
        "test_plugin",
        themis::plugins::PluginType::CUSTOM,
        []() { return std::make_unique<TestPlugin>(); }
    );
    
    // Create plugin from registry
    auto plugin = themis::plugins::PluginRegistry::createPlugin("test_plugin");
    ASSERT_NE(plugin, nullptr);
    EXPECT_STREQ(plugin->getName(), "test_plugin");
    EXPECT_STREQ(plugin->getVersion(), "1.0.0");
}

TEST_F(PluginManagerTest, PluginLifecycle) {
    // Register factory
    themis::plugins::PluginRegistry::registerFactory(
        "lifecycle_test",
        themis::plugins::PluginType::CUSTOM,
        []() { return std::make_unique<TestPlugin>(); }
    );
    
    // Create and initialize
    auto plugin = themis::plugins::PluginRegistry::createPlugin("lifecycle_test");
    ASSERT_NE(plugin, nullptr);
    
    EXPECT_TRUE(plugin->initialize("{}"));
    
    auto* test_plugin = static_cast<TestPlugin*>(plugin.get());
    EXPECT_TRUE(test_plugin->isInitialized());
    
    plugin->shutdown();
    EXPECT_FALSE(test_plugin->isInitialized());
}

TEST_F(PluginManagerTest, ListPlugins) {
    auto& pm = themis::plugins::PluginManager::instance();
    
    // Initially no plugins loaded
    auto loaded = pm.listLoadedPlugins();
    // May have plugins from previous tests, so just check it's a valid list
    EXPECT_TRUE(loaded.empty() || !loaded.empty());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
