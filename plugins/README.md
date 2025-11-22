# Hardware Acceleration Plugins - ThemisDB

## Übersicht

ThemisDB verwendet ein Plugin-System für Hardware-Beschleunigung. Backends werden als optionale DLLs/Shared Libraries verteilt und zur Laufzeit geladen, je nach verfügbarer Hardware.

## Plugin-Architektur

```
ThemisDB Core (themis_core.dll/.so)
    ↓
Backend Registry (lädt Plugins zur Laufzeit)
    ↓
Plugins (optional, je nach Hardware):
    - themis_accel_cuda.dll      (NVIDIA CUDA)
    - themis_accel_vulkan.dll    (Vulkan)
    - themis_accel_directx.dll   (DirectX 12)
    - themis_accel_hip.dll       (AMD HIP)
    - themis_accel_metal.dylib   (Apple Metal)
    etc.
```

## Plugin-Verzeichnisse

Standard-Suchpfade für Plugins:

| Platform | Standard-Pfad |
|----------|---------------|
| Windows | `C:/Program Files/ThemisDB/plugins` |
| Linux | `/usr/local/lib/themis/plugins` |
| macOS | `/usr/local/lib/themis/plugins` |
| Relativ | `./plugins` (neben Executable) |

## Plugin-Namenskonvention

Plugins müssen folgendem Schema folgen:

```
themis_accel_<backend>.<ext>

Beispiele:
- Windows: themis_accel_cuda.dll
- Linux:   themis_accel_cuda.so
- macOS:   themis_accel_metal.dylib
```

## Plugin-Entwicklung

### 1. Plugin-Struktur

```cpp
// my_plugin.cpp
#include "acceleration/plugin_loader.h"
#include "acceleration/compute_backend.h"

class MyPlugin : public BackendPlugin {
public:
    const char* pluginName() const noexcept override {
        return "My Acceleration Plugin";
    }
    
    const char* pluginVersion() const noexcept override {
        return "1.0.0";
    }
    
    BackendType backendType() const noexcept override {
        return BackendType::CUDA;  // oder andere
    }
    
    std::unique_ptr<IVectorBackend> createVectorBackend() override {
        return std::make_unique<MyVectorBackend>();
    }
    
    std::unique_ptr<IGraphBackend> createGraphBackend() override {
        return std::make_unique<MyGraphBackend>();
    }
    
    std::unique_ptr<IGeoBackend> createGeoBackend() override {
        return std::make_unique<MyGeoBackend>();
    }
};

// Export plugin entry point
THEMIS_DEFINE_PLUGIN(MyPlugin)
```

### 2. CMake Build

```cmake
cmake_minimum_required(VERSION 3.20)
project(themis_accel_myplugin)

add_library(themis_accel_myplugin SHARED
    my_plugin.cpp
    my_vector_backend.cpp
)

target_link_libraries(themis_accel_myplugin PRIVATE
    # Your dependencies here
)

install(TARGETS themis_accel_myplugin
    LIBRARY DESTINATION lib/themis/plugins
)
```

### 3. Backend-Implementation

Jedes Backend muss die entsprechenden Interfaces implementieren:

- `IVectorBackend` - für Vector-Operationen
- `IGraphBackend` - für Graph-Operationen  
- `IGeoBackend` - für Geo-Operationen

## Verfügbare Plugins

### CUDA Plugin (NVIDIA)

**Datei:** `themis_accel_cuda.dll/.so`

**Requirements:**
- CUDA Toolkit 11.0+
- NVIDIA GPU (Compute Capability 7.0+)
- Driver 450.80.02+

**Features:**
- Faiss GPU Integration
- Custom CUDA Kernels
- Async Compute Streams

**Status:** 🚧 In Development

---

### Vulkan Plugin (Cross-Platform)

**Datei:** `themis_accel_vulkan.dll/.so`

**Requirements:**
- Vulkan 1.2+
- Vulkan-fähige GPU
- Vulkan SDK

**Features:**
- Cross-Platform (Windows, Linux, Android)
- Compute Pipelines
- Memory Transfer Optimization

**Status:** 🚧 Planned

---

### DirectX Plugin (Windows)

**Datei:** `themis_accel_directx.dll`

**Requirements:**
- Windows 10 (1809+) or Windows 11
- DirectX 12 capable GPU
- DirectML SDK

**Features:**
- Windows-native
- DirectX 12 Compute Shaders
- DirectML Integration

**Status:** 🚧 Planned

---

### HIP Plugin (AMD)

**Datei:** `themis_accel_hip.so`

**Requirements:**
- AMD GPU (GCN 4.0+)
- ROCm Platform
- HIP Runtime

**Features:**
- AMD-native performance
- CUDA-like API
- ROCm Integration

**Status:** 🚧 Planned

---

## Plugin-Verwendung

### Automatisches Laden

```cpp
#include "acceleration/compute_backend.h"

// Auto-detect und laden aller verfügbaren Plugins
auto& registry = BackendRegistry::instance();
registry.autoDetect();

// Bestes verfügbares Backend verwenden
auto* backend = registry.getBestVectorBackend();
```

### Manuelles Laden

```cpp
// Spezifisches Plugin laden
registry.loadPlugin("./plugins/themis_accel_cuda.dll");

// Oder gesamtes Verzeichnis
registry.loadPlugins("./plugins");

// Backend verwenden
auto* cudaBackend = registry.getBackend(BackendType::CUDA);
```

### Konfiguration

```yaml
# config/acceleration.yaml
acceleration:
  plugin_directory: "./plugins"
  auto_load: true
  
  # Spezifische Plugins aktivieren/deaktivieren
  enabled_plugins:
    - cuda
    - vulkan
    
  disabled_plugins:
    - opengl  # Legacy, nicht verwenden
```

## Deployment

### Entwicklung

Für Entwicklung: Plugins im lokalen `./plugins` Verzeichnis:

```
project/
├── build/
│   └── themis_server.exe
└── plugins/
    ├── themis_accel_cuda.dll
    └── themis_accel_vulkan.dll
```

### Production (Windows)

```
C:/Program Files/ThemisDB/
├── bin/
│   └── themis_server.exe
└── plugins/
    ├── themis_accel_cuda.dll
    └── themis_accel_vulkan.dll
```

### Production (Linux)

```
/usr/local/
├── bin/
│   └── themis_server
└── lib/themis/plugins/
    ├── themis_accel_cuda.so
    └── themis_accel_vulkan.so
```

## Troubleshooting

### Plugin wird nicht geladen

**Symptom:**
```
Warning: Failed to load plugin: ./plugins/themis_accel_cuda.dll
```

**Lösungen:**
1. Prüfe ob Plugin-Datei existiert
2. Prüfe Datei-Permissions
3. Prüfe ob Dependencies (z.B. CUDA Runtime) installiert sind
4. Prüfe Plugin-Namenskonvention

### Plugin lädt aber Backend nicht verfügbar

**Symptom:**
```
Loaded plugin: CUDA Acceleration Plugin v1.0.0
Warning: CUDA backend not available
```

**Lösungen:**
1. Hardware nicht kompatibel (kein CUDA GPU)
2. Driver zu alt
3. Runtime-Libraries fehlen

### Falsches Backend wird gewählt

**Symptom:**
CPU wird verwendet obwohl GPU verfügbar

**Lösung:**
Explizit Backend wählen:
```cpp
auto* backend = registry.getBackend(BackendType::CUDA);
if (!backend || !backend->isAvailable()) {
    // Fallback
}
```

## Best Practices

1. **Immer CPU-Fallback bereitstellen**
   - CPU-Backend ist immer verfügbar
   - Graceful degradation

2. **Plugin-Verfügbarkeit prüfen**
   ```cpp
   if (auto* gpu = registry.getBestVectorBackend()) {
       if (gpu->type() != BackendType::CPU) {
           // GPU verfügbar
       }
   }
   ```

3. **Fehlerbehandlung**
   ```cpp
   try {
       backend->initialize();
   } catch (const std::exception& e) {
       // Fallback to CPU
   }
   ```

4. **Versionskompat ibilität**
   - Plugin-Version mit Core-Version abgleichen
   - ABI-Kompatibilität sicherstellen

## Lizenzierung

- **Core:** MIT License
- **Plugins:** Siehe jeweilige Plugin-Dokumentation
  - CUDA Plugin: Requires CUDA Toolkit (NVIDIA License)
  - DirectX Plugin: Requires Windows SDK (Microsoft License)
  - Vulkan Plugin: Open Source (MIT)

---

**Weitere Informationen:**
- [Hardware Acceleration Guide](HARDWARE_ACCELERATION.md)
- [Plugin Development Guide](../development/plugin_development.md)
- [API Reference](../api/acceleration_api.md)
