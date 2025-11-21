# Plugin Manifest Signature Verification

**Datum:** 21. November 2025  
**Feature:** Signatur-Verifikation für plugin.json Manifeste  
**Zweck:** Sicherstellung der Integrität von Plugin-Manifesten

---

## 🔒 Übersicht

ThemisDB verwendet eine Signatur-Verifikationsstrategie für Plugin-Manifeste (`plugin.json`), ähnlich wie für YAML-Konfigurationsdateien. Dies verhindert Manipulation von Plugin-Metadaten.

### Sicherheitsmodell

**Entwicklungsmodus (Debug Build):**
- Signaturen optional
- Warnung bei fehlenden Signaturen
- Warnung bei Hash-Mismatch, aber Plugin wird trotzdem geladen
- Erleichtert Entwicklung und Tests

**Produktionsmodus (Release Build):**
- Signaturen **erforderlich**
- Plugin wird **nicht** geladen ohne gültige Signatur
- Verhindert Manipulation und Supply-Chain-Angriffe
- Strikte Validierung

---

## 📝 Signatur-Format

### Signatur-Datei: `plugin.json.sig`

Die Signatur-Datei enthält den SHA256-Hash des Manifests:

```
a1b2c3d4e5f6789012345678901234567890abcdefabcdefabcdefabcdefabcd
```

**Eigenschaften:**
- Einzeilige Datei
- 64 Hex-Zeichen (SHA256-Hash)
- Keine zusätzlichen Metadaten
- Einfach zu generieren und verifizieren

---

## 🛠️ Signatur Generieren

### Mit Python-Tool

```bash
# Einzelnes Manifest signieren
python tools/sign_plugin_manifest.py plugins/blob/filesystem/plugin.json

# Output:
# ✓ Generated signature for plugins/blob/filesystem/plugin.json
#   SHA256: a1b2c3d4e5f6...
#   Signature file: plugins/blob/filesystem/plugin.json.sig
```

### Manuell mit OpenSSL

```bash
# SHA256-Hash berechnen
openssl dgst -sha256 -hex plugin.json | awk '{print $2}' > plugin.json.sig
```

### Manuell mit sha256sum (Linux)

```bash
sha256sum plugin.json | awk '{print $1}' > plugin.json.sig
```

---

## 🔍 Verifikationsprozess

### Automatische Verifikation

Der PluginManager verifiziert Manifeste automatisch beim Laden:

```cpp
// Beim Scannen von Plugins
auto& pm = PluginManager::instance();
pm.scanPluginDirectory("./plugins");  // Verifiziert alle plugin.json automatisch
```

**Verifikationsschritte:**
1. Prüfe ob `plugin.json.sig` existiert
2. Lese erwarteten Hash aus `.sig` Datei
3. Berechne tatsächlichen Hash von `plugin.json`
4. Vergleiche Hashes
5. Bei Mismatch: Fehler (Produktion) oder Warnung (Entwicklung)

### Logs

**Erfolgreiche Verifikation:**
```
[INFO] Manifest signature verified: ./plugins/blob/filesystem/plugin.json
```

**Fehlende Signatur (Produktion):**
```
[ERROR] Manifest signature file not found: ./plugins/blob/filesystem/plugin.json.sig
```

**Hash-Mismatch (Produktion):**
```
[ERROR] Manifest signature verification failed: hash mismatch
  Expected: a1b2c3d4...
  Actual:   f9e8d7c6...
```

**Warnung (Entwicklung):**
```
[WARN] Manifest signature file not found (development mode): ./plugins/blob/filesystem/plugin.json.sig
```

---

## 📁 Plugin-Struktur mit Signatur

```
plugins/
├── blob/
│   ├── filesystem/
│   │   ├── plugin.json          # Manifest
│   │   ├── plugin.json.sig      # ✅ Signatur (SHA256)
│   │   ├── themis_blob_fs.dll   # Binary (Windows)
│   │   ├── themis_blob_fs.so    # Binary (Linux)
│   │   └── themis_blob_fs.dylib # Binary (macOS)
│   └── webdav/
│       ├── plugin.json
│       ├── plugin.json.sig      # ✅ Signatur
│       └── themis_blob_webdav.dll
└── importers/
    └── postgres/
        ├── plugin.json
        ├── plugin.json.sig      # ✅ Signatur
        └── themis_import_pg.dll
```

---

## 🔐 Erweiterte Signatur-Strategien

### Aktuell: SHA256-Hash

**Vorteile:**
- ✅ Einfach zu generieren
- ✅ Schnelle Verifikation
- ✅ Keine Zertifikat-Infrastruktur nötig
- ✅ Schutz gegen Manipulation

**Nachteile:**
- ⚠️ Keine Authentifizierung (wer hat signiert?)
- ⚠️ Keine Nicht-Abstreitbarkeit

### Zukünftig: Digitale Signaturen (RSA/ECDSA)

**Geplante Erweiterung:**
```json
// plugin.json.sig (JSON format)
{
  "hash": "a1b2c3d4e5f6...",
  "signature": "MIIBIjANBgkq...",
  "certificate": "-----BEGIN CERTIFICATE-----...",
  "issuer": "CN=ThemisDB Official Plugins, O=ThemisDB",
  "timestamp": 1732176000
}
```

**Integration mit bestehendem System:**
- Nutzt `storage/security_signature.h` (SecuritySignature struct)
- Nutzt `storage/security_signature_manager.h` (RocksDB-basiert)
- Nutzt `acceleration/plugin_security.h` (PluginSignature)

---

## 🚀 Build-Integration

### CMake Build-Schritt

```cmake
# plugins/blob/filesystem/CMakeLists.txt

# Erstelle Binary
add_library(themis_blob_fs SHARED
    filesystem_plugin.cpp
)

# Signiere Manifest nach Build
add_custom_command(TARGET themis_blob_fs POST_BUILD
    COMMAND ${PYTHON_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/sign_plugin_manifest.py
            ${CMAKE_CURRENT_SOURCE_DIR}/plugin.json
    COMMENT "Signing plugin manifest: filesystem"
)
```

### CI/CD Pipeline

```yaml
# .github/workflows/build-plugins.yml
- name: Build Plugins
  run: cmake --build build --target all

- name: Sign Plugin Manifests
  run: |
    python tools/sign_plugin_manifest.py plugins/blob/filesystem/plugin.json
    python tools/sign_plugin_manifest.py plugins/blob/webdav/plugin.json
    python tools/sign_plugin_manifest.py plugins/importers/postgres/plugin.json

- name: Verify Signatures
  run: |
    # Test in production mode
    cmake -DCMAKE_BUILD_TYPE=Release ...
    ./bin/themis_server --verify-plugins
```

---

## 🧪 Testing

### Unit Tests

```cpp
// tests/test_plugin_manifest_signature.cpp
TEST(PluginManagerTest, ManifestSignatureVerification) {
    // Create test manifest
    std::string manifest_path = "./test_plugin.json";
    createTestManifest(manifest_path);
    
    // Generate signature
    std::string hash = PluginManager::calculateFileHash(manifest_path);
    std::ofstream sig(manifest_path + ".sig");
    sig << hash << std::endl;
    sig.close();
    
    // Verify
    std::string error;
    PluginManager pm;
    EXPECT_TRUE(pm.verifyManifestSignature(manifest_path, error));
}

TEST(PluginManagerTest, ManifestSignatureMismatch) {
    std::string manifest_path = "./test_plugin.json";
    createTestManifest(manifest_path);
    
    // Wrong signature
    std::ofstream sig(manifest_path + ".sig");
    sig << "0000000000000000000000000000000000000000000000000000000000000000" << std::endl;
    sig.close();
    
    std::string error;
    PluginManager pm;
    
#ifdef NDEBUG
    EXPECT_FALSE(pm.verifyManifestSignature(manifest_path, error));
#else
    EXPECT_TRUE(pm.verifyManifestSignature(manifest_path, error));  // Allowed in dev
#endif
}
```

---

## 📊 Vergleich: YAML vs. JSON Signatur

| Aspekt | YAML Config | JSON Manifest |
|--------|-------------|---------------|
| **Datei** | config.yaml | plugin.json |
| **Signatur-Datei** | RocksDB (SecuritySignature) | plugin.json.sig (Datei) |
| **Hash-Algorithmus** | SHA256 | SHA256 |
| **Speicherort** | RocksDB (security_sig:*) | Filesystem (neben Manifest) |
| **Verifikation** | SecuritySignatureManager | PluginManager |
| **Entwicklung** | Optional | Optional |
| **Produktion** | Optional | **Erforderlich** |

**Warum unterschiedlich?**
- YAML: Zentrale Konfiguration → RocksDB-basiert
- JSON: Verteilte Plugins → Filesystem-basiert (portabel)

---

## 🔧 Fehlerbehebung

### Problem: "Manifest signature file not found"

**Lösung:**
```bash
# Generiere Signatur
python tools/sign_plugin_manifest.py plugin.json
```

### Problem: "Hash mismatch"

**Ursache:** Manifest wurde nach Signierung geändert

**Lösung:**
```bash
# Neu signieren
python tools/sign_plugin_manifest.py plugin.json
```

### Problem: "Plugin wird nicht geladen (Produktion)"

**Prüfung:**
```bash
# Prüfe ob Signatur existiert
ls -la plugin.json.sig

# Prüfe Hash
sha256sum plugin.json
cat plugin.json.sig

# Vergleiche
```

---

## 🎯 Best Practices

1. **Immer signieren vor Deployment**
   ```bash
   python tools/sign_plugin_manifest.py plugin.json
   ```

2. **Signaturen in Version Control**
   - Committe `plugin.json.sig` zusammen mit `plugin.json`
   - Nie nur Manifest ohne Signatur commiten

3. **CI/CD Automatisierung**
   - Automatisches Signieren im Build-Prozess
   - Verifikation vor Package-Erstellung

4. **Versionierung**
   - Bei Manifest-Änderung: Neu signieren
   - Bei Version-Bump: Neu signieren

5. **Security Audits**
   - Regelmäßige Prüfung aller Signaturen
   - Rotation von Signatur-Schlüsseln (bei RSA/ECDSA)

---

## 📚 Weiterführende Dokumentation

- `include/plugins/plugin_manager.h` - PluginManager Interface
- `src/plugins/plugin_manager.cpp` - Verifikations-Implementation
- `tools/sign_plugin_manifest.py` - Signatur-Generator
- `include/storage/security_signature.h` - Bestehende Signatur-Infrastruktur
- `docs/plugins/PLUGIN_MIGRATION.md` - Plugin-System Architektur

---

**Status:** ✅ Implementiert  
**Version:** 1.0.0  
**Nächste Schritte:** RSA/ECDSA digitale Signaturen (optional)
