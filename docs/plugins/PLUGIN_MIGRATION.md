# Plugin-System Migration & Konsolidierung

**Datum:** 21. November 2025  
**Zweck:** Zusammenführung bestehender DLL-Loader zu einem einheitlichen Plugin-System

---

## 🔍 Analyse: Bestehende DLL-Loader

### 1. acceleration/plugin_loader.h + .cpp
**Status:** ✅ **VOLLSTÄNDIG IMPLEMENTIERT**

**Features:**
- Platform-agnostic DLL loading (Windows/Linux/macOS)
- Security verification (plugin_security.h integration)
- Plugin discovery from directory
- Signature verification (production)
- Hash-based integrity checks
- Audit logging

**Interface:**
```cpp
class PluginLoader {
    bool loadPlugin(const std::string& libraryPath);
    size_t loadPluginsFromDirectory(const std::string& directoryPath);
    BackendPlugin* getPlugin(const std::string& pluginName) const;
};
```

**Entry Point:**
```cpp
extern "C" THEMIS_PLUGIN_EXPORT BackendPlugin* CreateBackendPlugin();
```

**Verwendung:**
- Compute backends (CUDA, OpenCL, Vulkan, DirectX, HIP, OneAPI)
- Prefix: `themis_accel_*.dll`

---

### 2. security/hsm_provider_pkcs11.cpp
**Status:** ✅ **AD-HOC IMPLEMENTATION**

**Features:**
- Dynamisches Laden von PKCS#11-Bibliotheken
- Windows: LoadLibraryA / GetProcAddress
- Linux: dlopen / dlsym
- Function pointer loading: C_GetFunctionList

**Code:**
```cpp
// src/security/hsm_provider_pkcs11.cpp (Zeilen 32-52)
#ifdef _WIN32
    lib_ = LoadLibraryA(path.c_str());
    auto getFn = (CK_C_GetFunctionList)GetProcAddress((HMODULE)lib_, "C_GetFunctionList");
#else
    lib_ = dlopen(path.c_str(), RTLD_NOW);
    auto getFn = (CK_C_GetFunctionList)dlsym(lib_, "C_GetFunctionList");
#endif
```

**Problem:**
- Keine Security-Verifikation
- Keine Hash-Checks
- Duplizierter Code für DLL-Loading
- Kein Plugin-Registry

---

### 3. acceleration/zluda_backend.cpp
**Status:** ✅ **AD-HOC IMPLEMENTATION**

**Features:**
- Dynamisches Laden von libcuda.so.zluda (ZLUDA = CUDA on AMD)
- Fallback zu libcuda.so
- Function pointer loading für CUDA API

**Code:**
```cpp
// src/acceleration/zluda_backend.cpp (Zeilen 95-97)
zludaLib_ = dlopen("libcuda.so.zluda", RTLD_NOW);
if (!zludaLib_) {
    zludaLib_ = dlopen("libcuda.so", RTLD_NOW);
}
```

**Problem:**
- Hardcoded library names
- Keine Konfiguration
- Keine Fallback-Strategie
- Duplizierter DLL-Loading-Code

---

## 🎯 Konsolidierungs-Strategie

### Phase 1: Bestehenden PluginLoader erweitern (DONE)
✅ **Neue Komponenten:**
1. `include/plugins/plugin_interface.h` - Unified interface
2. `include/plugins/plugin_manager.h` - Erweitert acceleration/PluginLoader
3. `docs/plugins/PLUGIN_MIGRATION.md` - Dieses Dokument

**Key Design Decisions:**
- ✅ **Reuse** acceleration/plugin_loader.cpp für Platform-Loading
- ✅ **Reuse** acceleration/plugin_security.h für Verification
- ✅ **Extend** mit Plugin Manifests (plugin.json)
- ✅ **Extend** mit Type-based Registry

---

### Phase 2: Migration bestehender Loader

#### 2.1 HSM Provider Migration
**Vorher:**
```cpp
// src/security/hsm_provider_pkcs11.cpp
void* lib_ = dlopen(path.c_str(), RTLD_NOW);
auto getFn = (CK_C_GetFunctionList)dlsym(lib_, "C_GetFunctionList");
```

**Nachher:**
```cpp
// src/security/hsm_provider_pkcs11.cpp
class PKCS11Plugin : public IThemisPlugin {
    PluginType getType() const override { return PluginType::HSM_PROVIDER; }
    void* getInstance() override { return &hsm_provider_; }
};

// Auto-load via PluginManager
auto plugin = PluginManager::instance().loadPlugin("pkcs11");
auto* provider = static_cast<HSMProvider*>(plugin->getInstance());
```

**Benefits:**
- ✅ Security verification
- ✅ Unified loading
- ✅ Plugin registry
- ✅ Hot-reload support

---

#### 2.2 ZLUDA Backend Migration
**Vorher:**
```cpp
// src/acceleration/zluda_backend.cpp
zludaLib_ = dlopen("libcuda.so.zluda", RTLD_NOW);
```

**Nachher:**
```cpp
// plugins/compute/zluda/themis_accel_zluda.cpp
class ZLUDAPlugin : public acceleration::BackendPlugin {
    // Already uses BackendPlugin interface!
    // Just move to separate DLL
};

// plugin.json
{
  "name": "zluda",
  "type": "compute_backend",
  "binary": {
    "linux": "themis_accel_zluda.so"
  },
  "auto_load": false  // Load on-demand
}
```

**Benefits:**
- ✅ ZLUDA nur laden wenn benötigt (nicht standardmäßig)
- ✅ Kleinere Core-Binary
- ✅ Einfacher zu testen (AMD-Hardware optional)

---

### Phase 3: Neue Plugin-Typen

#### 3.1 Blob Storage Plugins
**Structure:**
```
plugins/
├── blob/
│   ├── filesystem/
│   │   ├── themis_blob_fs.dll
│   │   ├── plugin.json
│   │   └── CMakeLists.txt
│   ├── webdav/
│   │   ├── themis_blob_webdav.dll
│   │   ├── plugin.json
│   │   └── CMakeLists.txt
│   └── s3/
│       ├── themis_blob_s3.dll
│       ├── plugin.json
│       └── CMakeLists.txt
```

**plugin.json (Filesystem):**
```json
{
  "name": "filesystem",
  "version": "1.0.0",
  "type": "blob_storage",
  "description": "Local filesystem blob storage",
  "binary": {
    "windows": "themis_blob_fs.dll",
    "linux": "themis_blob_fs.so",
    "macos": "themis_blob_fs.dylib"
  },
  "dependencies": [],
  "capabilities": {
    "streaming": false,
    "batching": true,
    "thread_safe": true
  },
  "config_schema": {
    "base_path": {"type": "string", "default": "./data/blobs"}
  },
  "auto_load": true,
  "load_priority": 10
}
```

---

#### 3.2 Importer Plugins
**Structure:**
```
plugins/
├── importers/
│   ├── postgres/
│   │   ├── themis_import_pg.dll
│   │   ├── plugin.json
│   │   └── CMakeLists.txt
│   └── csv/
│       ├── themis_import_csv.dll
│       ├── plugin.json
│       └── CMakeLists.txt
```

---

## 🔧 Code Changes Required

### 1. Update CMakeLists.txt
```cmake
# Root CMakeLists.txt
option(BUILD_PLUGINS "Build plugins as separate DLLs" ON)

if(BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

# plugins/CMakeLists.txt (NEW)
add_subdirectory(blob)
add_subdirectory(importers)
add_subdirectory(compute)  # Existing acceleration plugins

# plugins/blob/filesystem/CMakeLists.txt (NEW)
add_library(themis_blob_fs SHARED
    filesystem_plugin.cpp
    ../../../src/storage/blob_backend_filesystem.cpp
)
target_link_libraries(themis_blob_fs PRIVATE themis_core)
set_target_properties(themis_blob_fs PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/blob"
)
```

---

### 2. Update Config Loading
```cpp
// src/main_server.cpp
#include "plugins/plugin_manager.h"

int main() {
    // Initialize plugin system
    auto& pm = PluginManager::instance();
    pm.scanPluginDirectory("./plugins");
    pm.autoLoadPlugins();
    
    // Existing code...
}
```

---

### 3. Update HSM Provider
```cpp
// src/security/hsm_provider_pkcs11.cpp
// BEFORE: Direct dlopen
void* lib_ = dlopen(config_.library_path.c_str(), RTLD_NOW);

// AFTER: Use PluginManager
auto plugin = PluginManager::instance().loadPluginFromPath(
    config_.library_path,
    json{{"slot_id", config_.slot_id}}.dump()
);
```

---

## 📊 Benefits Summary

| Aspect | Before (3 separate loaders) | After (Unified) |
|--------|----------------------------|-----------------|
| **Code Duplication** | 3x DLL loading code | 1x shared code |
| **Security** | Only acceleration/ has it | All plugins verified |
| **Discovery** | Manual path specification | Auto-discovery |
| **Hot-Reload** | Not supported | Supported |
| **Manifest** | No metadata | plugin.json |
| **Dependency Mgmt** | Manual | Automatic |
| **Type Safety** | Generic void* | Type-based registry |

---

## 🚀 Migration Timeline

### Week 1: Foundation
- [x] Create unified plugin interface
- [x] Create PluginManager (extends PluginLoader)
- [ ] Write migration documentation
- [ ] Test plan

### Week 2: Migrate Existing
- [ ] Refactor HSM Provider to use PluginManager
- [ ] Extract ZLUDA to separate plugin DLL
- [ ] Update build system

### Week 3: New Plugins
- [ ] Blob Storage plugins (Filesystem, WebDAV)
- [ ] Importer plugins (PostgreSQL, CSV)
- [ ] Integration tests

### Week 4: Documentation
- [ ] Plugin Development Guide
- [ ] API Reference
- [ ] Example plugins

---

## 📝 Example: Migrating HSM Provider

**Step 1: Create Plugin Wrapper**
```cpp
// plugins/hsm/pkcs11/pkcs11_plugin.cpp
#include "plugins/plugin_interface.h"
#include "security/hsm_provider.h"

class PKCS11Plugin : public IThemisPlugin {
private:
    std::unique_ptr<HSMProvider> provider_;
    
public:
    const char* getName() const override { return "pkcs11"; }
    const char* getVersion() const override { return "1.0.0"; }
    PluginType getType() const override { return PluginType::HSM_PROVIDER; }
    
    PluginCapabilities getCapabilities() const override {
        PluginCapabilities caps;
        caps.thread_safe = true;
        return caps;
    }
    
    bool initialize(const char* config_json) override {
        auto config = json::parse(config_json);
        HSMConfig hsm_config;
        hsm_config.library_path = config["library_path"];
        hsm_config.slot_id = config["slot_id"];
        hsm_config.pin = config["pin"];
        
        provider_ = std::make_unique<HSMProvider>(hsm_config);
        return provider_->initialize();
    }
    
    void shutdown() override {
        if (provider_) provider_->finalize();
    }
    
    void* getInstance() override {
        return provider_.get();
    }
};

THEMIS_PLUGIN_IMPL(PKCS11Plugin)
```

**Step 2: Create Manifest**
```json
// plugins/hsm/pkcs11/plugin.json
{
  "name": "pkcs11",
  "version": "1.0.0",
  "type": "hsm_provider",
  "description": "PKCS#11 Hardware Security Module Provider",
  "binary": {
    "windows": "themis_hsm_pkcs11.dll",
    "linux": "themis_hsm_pkcs11.so"
  },
  "dependencies": [
    "pkcs11-library (runtime)"
  ],
  "config_schema": {
    "library_path": {"type": "string", "required": true},
    "slot_id": {"type": "number", "default": 0},
    "pin": {"type": "string", "required": true}
  },
  "auto_load": false
}
```

**Step 3: Update Usage**
```cpp
// OLD (src/security/some_service.cpp)
HSMConfig config;
config.library_path = "/usr/lib/softhsm/libsofthsm2.so";
config.slot_id = 0;
config.pin = "1234";
auto hsm = std::make_unique<HSMProvider>(config);
hsm->initialize();

// NEW
auto plugin = PluginManager::instance().loadPlugin("pkcs11");
auto* hsm = static_cast<HSMProvider*>(plugin->getInstance());
```

---

**Status:** ✅ Design Complete, Ready for Implementation  
**Next:** Implementation der PluginManager-Klasse
