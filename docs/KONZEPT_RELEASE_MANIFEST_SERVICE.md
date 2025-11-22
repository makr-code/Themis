# Release Manifest Service für Hot-Reload - Konzept

## Neue Anforderung

Für die Hot-Reload-Funktionalität benötigen wir einen automatisierten Signaturen- und Manifest-Datenbank-Service für alle Release-DLLs und Themis-Dateien.

## Vorhandene Infrastruktur

### ✅ Bereits vorhanden in ThemisDB:

1. **Plugin Security Infrastructure** (`acceleration/plugin_security.h/cpp`)
   - `PluginSecurityVerifier` - Signaturverifikation
   - `PluginMetadata` - Metadata-Struktur mit Signatur-Support
   - `PluginSignature` - SHA-256 Hash + digitale Signatur
   - `PluginSecurityAuditor` - Security Event Logging

2. **PKI/Signing Infrastructure** (`security/`)
   - `SigningService` - Abstrakte Signing-Schnittstelle
   - `CMSSigningService` - CMS/PKCS#7-basierte Signaturen
   - `PKIKeyProvider` - Schlüsselverwaltung
   - `TimestampAuthority` - Trusted Timestamps

3. **Plugin Manager** (`plugins/plugin_manager.h`)
   - `PluginManifest` - Manifest-Struktur
   - `loadManifest()` - JSON-basiertes Manifest-Laden
   - `verifyManifestSignature()` - Signatur-Verifikation

4. **Backup Manager** (`storage/backup_manager.h`)
   - Manifest-Generierung für Backups
   - Metadata-Management

## Architektur: Release Manifest Service

### Komponenten-Übersicht

```
┌─────────────────────────────────────────────────────────────┐
│                   Release Manifest Service                   │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────────────┐        ┌──────────────────────┐  │
│  │ Manifest Generator   │───────▶│  Manifest Database   │  │
│  │ - File Scanning      │        │  - RocksDB Storage   │  │
│  │ - Hash Calculation   │        │  - Version Index     │  │
│  │ - Signature Creation │        │  - File Registry     │  │
│  └──────────────────────┘        └──────────────────────┘  │
│            │                                │                │
│            │                                │                │
│            ▼                                ▼                │
│  ┌──────────────────────┐        ┌──────────────────────┐  │
│  │  PKI Signing         │        │  Manifest Verifier   │  │
│  │  - CMS Signatures    │        │  - Hash Validation   │  │
│  │  - Timestamp Auth    │        │  - Signature Check   │  │
│  │  - Certificate Chain │        │  - Version Check     │  │
│  └──────────────────────┘        └──────────────────────┘  │
│                                              │                │
└──────────────────────────────────────────────┼───────────────┘
                                               │
                                               ▼
                                    ┌──────────────────────┐
                                    │  Hot-Reload Engine   │
                                    │  - Download Release  │
                                    │  - Verify Manifest   │
                                    │  - Apply Update      │
                                    │  - Rollback Support  │
                                    └──────────────────────┘
```

### Datenmodell

#### 1. ReleaseManifest

```cpp
struct ReleaseManifest {
    // Release Info
    std::string version;              // z.B. "1.2.3"
    std::string tag_name;             // z.B. "v1.2.3"
    std::string release_notes;        // Änderungslog
    std::chrono::system_clock::time_point release_date;
    bool is_critical;                 // Kritisches Security-Update?
    
    // Files in this release
    std::vector<ReleaseFile> files;
    
    // Signature & Verification
    std::string manifest_hash;        // SHA-256 des gesamten Manifests
    std::string signature;            // CMS/PKCS#7 Signatur
    std::string signing_certificate;  // X.509 Zertifikat
    std::string timestamp_token;      // RFC 3161 Timestamp
    
    // Metadata
    std::string build_commit;         // Git Commit Hash
    std::string build_date;           // Build-Zeitpunkt
    std::string compiler_version;     // Compiler Info
    
    // Dependencies
    std::vector<std::string> dependencies;  // Abhängigkeiten zu anderen Komponenten
    
    // Minimum required version for upgrade
    std::string min_upgrade_from;     // z.B. "1.0.0"
    
    // JSON Schema Version
    int schema_version = 1;
};

struct ReleaseFile {
    // File Identity
    std::string path;                 // Relativer Pfad, z.B. "bin/themis_server"
    std::string type;                 // "executable", "library", "config", "data"
    
    // Hash & Size
    std::string sha256_hash;          // SHA-256 Hash der Datei
    uint64_t size_bytes;              // Dateigröße
    
    // Signature
    std::string file_signature;       // Individuelle Datei-Signatur
    
    // Platform
    std::string platform;             // "windows", "linux", "macos"
    std::string architecture;         // "x64", "arm64"
    
    // Permissions (Unix)
    std::string permissions;          // z.B. "0755" für Executables
    
    // Download Info
    std::string download_url;         // GitHub Release Asset URL
    
    // File-specific metadata
    nlohmann::json metadata;          // Zusätzliche Metadaten
};
```

#### 2. ManifestDatabase Schema

**RocksDB Column Families:**

```cpp
// Column Families
CF: "release_manifests"     // version -> ReleaseManifest (JSON)
CF: "file_registry"         // path:version -> ReleaseFile (JSON)
CF: "signature_cache"       // hash -> Signature Verification Result
CF: "download_cache"        // version:file -> lokaler Pfad
```

**Keys:**

```
release_manifests/
  1.0.0 -> ReleaseManifest JSON
  1.1.0 -> ReleaseManifest JSON
  1.2.0 -> ReleaseManifest JSON

file_registry/
  bin/themis_server:1.2.0 -> ReleaseFile JSON
  lib/themis_core.so:1.2.0 -> ReleaseFile JSON

signature_cache/
  <sha256_hash> -> {"verified": true, "timestamp": ..., "certificate": ...}

download_cache/
  1.2.0:themis_server -> "/tmp/themis_updates/1.2.0/themis_server"
```

### Implementierung

#### 1. Manifest Generator (Build-Zeit)

```cpp
class ReleaseManifestGenerator {
public:
    ReleaseManifestGenerator(
        std::shared_ptr<SigningService> signing_service,
        std::shared_ptr<TimestampAuthority> tsa
    );
    
    // Generate manifest for a release directory
    ReleaseManifest generateManifest(
        const std::string& release_dir,
        const std::string& version,
        const ReleaseMetadata& metadata
    );
    
    // Sign manifest with PKI
    void signManifest(ReleaseManifest& manifest);
    
    // Export manifest to JSON
    void exportManifest(
        const ReleaseManifest& manifest,
        const std::string& output_path
    );
    
private:
    std::string calculateFileHash(const std::string& file_path);
    std::string signFile(const std::string& file_path);
    std::string getTimestamp();
    
    std::shared_ptr<SigningService> signing_service_;
    std::shared_ptr<TimestampAuthority> tsa_;
};
```

#### 2. Manifest Database (Runtime)

```cpp
class ManifestDatabase {
public:
    ManifestDatabase(
        std::shared_ptr<RocksDBWrapper> storage,
        std::shared_ptr<PluginSecurityVerifier> verifier
    );
    
    // Store manifest in database
    bool storeManifest(const ReleaseManifest& manifest);
    
    // Retrieve manifest by version
    std::optional<ReleaseManifest> getManifest(const std::string& version);
    
    // Get latest manifest
    std::optional<ReleaseManifest> getLatestManifest();
    
    // List all available versions
    std::vector<std::string> listVersions() const;
    
    // Verify manifest integrity
    bool verifyManifest(const ReleaseManifest& manifest);
    
    // Check if file exists and is valid
    bool verifyFile(const std::string& path, const std::string& version);
    
    // Get file from registry
    std::optional<ReleaseFile> getFile(
        const std::string& path,
        const std::string& version
    );
    
private:
    std::shared_ptr<RocksDBWrapper> storage_;
    std::shared_ptr<PluginSecurityVerifier> verifier_;
    
    rocksdb::ColumnFamilyHandle* cf_manifests_;
    rocksdb::ColumnFamilyHandle* cf_files_;
    rocksdb::ColumnFamilyHandle* cf_signatures_;
    rocksdb::ColumnFamilyHandle* cf_cache_;
};
```

#### 3. Hot-Reload Engine

```cpp
class HotReloadEngine {
public:
    HotReloadEngine(
        std::shared_ptr<ManifestDatabase> manifest_db,
        std::shared_ptr<UpdateChecker> update_checker
    );
    
    // Download and verify a release
    struct DownloadResult {
        bool success;
        std::string error_message;
        std::string download_path;
        ReleaseManifest manifest;
    };
    
    DownloadResult downloadRelease(const std::string& version);
    
    // Apply hot-reload (atomic operation)
    struct ReloadResult {
        bool success;
        std::string error_message;
        std::vector<std::string> files_updated;
        std::string rollback_id;  // For rollback
    };
    
    ReloadResult applyHotReload(
        const std::string& version,
        bool verify_only = false  // Dry-run mode
    );
    
    // Rollback to previous version
    bool rollback(const std::string& rollback_id);
    
    // Verify release before applying
    struct VerificationResult {
        bool verified;
        std::string error_message;
        std::vector<std::string> warnings;
    };
    
    VerificationResult verifyRelease(const ReleaseManifest& manifest);
    
    // Check compatibility
    bool isCompatibleUpgrade(
        const std::string& current_version,
        const std::string& target_version
    );
    
private:
    std::shared_ptr<ManifestDatabase> manifest_db_;
    std::shared_ptr<UpdateChecker> update_checker_;
    
    // Download file with resume support
    bool downloadFile(const ReleaseFile& file, const std::string& dest);
    
    // Create backup before update
    std::string createBackup();
    
    // Atomic file replacement
    bool atomicReplace(const std::string& src, const std::string& dst);
};
```

### GitHub Actions Workflow für Manifest-Generierung

```yaml
# .github/workflows/release-manifest.yml
name: Generate Release Manifest

on:
  release:
    types: [published]

jobs:
  generate-manifest:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3
      
      - name: Download Release Assets
        run: |
          gh release download ${{ github.event.release.tag_name }} \
            --dir ./release_assets
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
      
      - name: Generate Manifest
        run: |
          ./tools/generate_release_manifest.sh \
            --version ${{ github.event.release.tag_name }} \
            --release-dir ./release_assets \
            --output manifest.json
      
      - name: Sign Manifest
        run: |
          # Sign with PKI certificate
          openssl cms -sign \
            -in manifest.json \
            -out manifest.json.sig \
            -signer ${{ secrets.SIGNING_CERT }} \
            -inkey ${{ secrets.SIGNING_KEY }} \
            -binary -outform DER
      
      - name: Upload Manifest to Release
        run: |
          gh release upload ${{ github.event.release.tag_name }} \
            manifest.json \
            manifest.json.sig
        env:
          GITHUB_TOKEN: ${{ secrets.GITHUB_TOKEN }}
      
      - name: Update Manifest Database
        run: |
          # Push to manifest database API
          curl -X POST https://manifest-db.themisdb.io/api/manifests \
            -H "Authorization: Bearer ${{ secrets.MANIFEST_DB_TOKEN }}" \
            -F "manifest=@manifest.json" \
            -F "signature=@manifest.json.sig"
```

### Manifest-Generierungs-Tool

```cpp
// tools/generate_release_manifest.cpp
int main(int argc, char* argv[]) {
    // Parse arguments
    cxxopts::Options options("generate_release_manifest", 
        "Generate signed manifest for ThemisDB release");
    
    options.add_options()
        ("version", "Release version", cxxopts::value<std::string>())
        ("release-dir", "Release directory", cxxopts::value<std::string>())
        ("output", "Output manifest file", cxxopts::value<std::string>())
        ("signing-key", "Path to signing key", cxxopts::value<std::string>())
        ("signing-cert", "Path to signing certificate", cxxopts::value<std::string>());
    
    auto result = options.parse(argc, argv);
    
    // Initialize signing service
    auto signing_service = createCMSSigningService(
        result["signing-cert"].as<std::string>(),
        result["signing-key"].as<std::string>()
    );
    
    // Initialize generator
    ReleaseManifestGenerator generator(signing_service, nullptr);
    
    // Generate manifest
    ReleaseMetadata metadata;
    metadata.release_notes = "...";  // From GitHub release
    metadata.is_critical = detectCritical();
    
    auto manifest = generator.generateManifest(
        result["release-dir"].as<std::string>(),
        result["version"].as<std::string>(),
        metadata
    );
    
    // Sign manifest
    generator.signManifest(manifest);
    
    // Export to JSON
    generator.exportManifest(
        manifest,
        result["output"].as<std::string>()
    );
    
    return 0;
}
```

### API Endpoints für Hot-Reload

```cpp
// GET /api/updates/manifests/:version
http::response<http::string_body> handleGetManifest(
    const http::request<http::string_body>& req
) {
    auto version = extractPathParam(req.target(), "/api/updates/manifests/");
    
    auto manifest = manifest_db_->getManifest(version);
    if (!manifest) {
        return makeErrorResponse(http::status::not_found,
            "Manifest not found for version: " + version, req);
    }
    
    // Convert to JSON
    nlohmann::json response = manifestToJson(*manifest);
    return createJsonResponse(http::status::ok, response, req);
}

// POST /api/updates/apply/:version
http::response<http::string_body> handleApplyUpdate(
    const http::request<http::string_body>& req
) {
    // Require admin authentication
    if (auto err = requireAccess(req, "admin", "update:apply", req.target())) {
        return *err;
    }
    
    auto version = extractPathParam(req.target(), "/api/updates/apply/");
    
    // Dry-run check first
    auto verify_result = hot_reload_engine_->verifyRelease(version);
    if (!verify_result.verified) {
        return makeErrorResponse(http::status::bad_request,
            "Update verification failed: " + verify_result.error_message, req);
    }
    
    // Apply hot-reload
    auto reload_result = hot_reload_engine_->applyHotReload(version);
    
    nlohmann::json response;
    response["success"] = reload_result.success;
    response["files_updated"] = reload_result.files_updated;
    response["rollback_id"] = reload_result.rollback_id;
    
    if (!reload_result.success) {
        response["error"] = reload_result.error_message;
        return createJsonResponse(http::status::internal_server_error, 
            response, req);
    }
    
    return createJsonResponse(http::status::ok, response, req);
}

// POST /api/updates/rollback/:rollback_id
http::response<http::string_body> handleRollback(
    const http::request<http::string_body>& req
) {
    // Require admin authentication
    if (auto err = requireAccess(req, "admin", "update:rollback", req.target())) {
        return *err;
    }
    
    auto rollback_id = extractPathParam(req.target(), "/api/updates/rollback/");
    
    bool success = hot_reload_engine_->rollback(rollback_id);
    
    nlohmann::json response;
    response["success"] = success;
    
    if (success) {
        return createJsonResponse(http::status::ok, response, req);
    } else {
        response["error"] = "Rollback failed";
        return createJsonResponse(http::status::internal_server_error, 
            response, req);
    }
}
```

### Sicherheitskonzept

#### 1. Signaturverifikation

**Prozess:**
```
1. Download manifest.json und manifest.json.sig von GitHub
2. Verifiziere CMS-Signatur gegen vertrauenswürdiges Zertifikat
3. Prüfe Timestamp gegen TSA
4. Verifiziere Certificate Chain bis zum Root CA
5. Prüfe Certificate Revocation (CRL/OCSP)
```

**Implementation:**
```cpp
bool ManifestDatabase::verifyManifest(const ReleaseManifest& manifest) {
    // 1. Verify CMS signature
    if (!verifier_->verifySignature(
        manifest.manifest_hash,
        manifest.signature,
        manifest.signing_certificate)) {
        return false;
    }
    
    // 2. Verify timestamp
    if (!verifyTimestamp(manifest.timestamp_token)) {
        return false;
    }
    
    // 3. Verify certificate chain
    if (!verifier_->verifyCertificateChain(manifest.signing_certificate)) {
        return false;
    }
    
    // 4. Check revocation
    if (policy_.checkRevocation) {
        if (!verifier_->checkCRL(manifest.signing_certificate)) {
            return false;
        }
    }
    
    return true;
}
```

#### 2. File Integrity

**Jede Datei wird doppelt verifiziert:**
```cpp
bool HotReloadEngine::verifyFile(const ReleaseFile& file) {
    // 1. Calculate actual hash
    std::string actual_hash = calculateFileHash(file.path);
    
    // 2. Compare with manifest
    if (actual_hash != file.sha256_hash) {
        LOG_ERROR("Hash mismatch for {}: expected {}, got {}",
            file.path, file.sha256_hash, actual_hash);
        return false;
    }
    
    // 3. Verify individual file signature
    if (!file.file_signature.empty()) {
        if (!verifyFileSignature(file)) {
            LOG_ERROR("Signature verification failed for {}", file.path);
            return false;
        }
    }
    
    return true;
}
```

#### 3. Rollback-Sicherheit

**Automatisches Backup vor Update:**
```cpp
std::string HotReloadEngine::createBackup() {
    std::string rollback_id = generateUUID();
    std::string backup_dir = "/var/lib/themisdb/rollback/" + rollback_id;
    
    // Backup all files that will be replaced
    for (const auto& file : current_files_) {
        std::filesystem::copy_file(
            file.path,
            backup_dir / file.path,
            std::filesystem::copy_options::overwrite_existing
        );
    }
    
    // Store rollback metadata
    nlohmann::json metadata;
    metadata["rollback_id"] = rollback_id;
    metadata["timestamp"] = getCurrentTimestamp();
    metadata["files"] = current_files_;
    
    std::ofstream(backup_dir / "rollback.json") << metadata.dump(2);
    
    return rollback_id;
}
```

### Deployment-Workflow

```
┌──────────────────┐
│ 1. Create Release│
│    on GitHub     │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 2. Build Assets  │
│    (CI/CD)       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 3. Generate      │
│    Manifest      │
│    (automated)   │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 4. Sign Manifest │
│    with PKI      │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 5. Upload to     │
│    GitHub Release│
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 6. Update Checker│
│    detects new   │
│    release       │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 7. Download &    │
│    verify        │
│    manifest      │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 8. Admin approves│
│    hot-reload    │
│    (or auto)     │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ 9. Apply update  │
│    with rollback │
│    support       │
└──────────────────┘
```

## Zusammenfassung

Das Release Manifest Service Konzept baut auf der **vorhandenen ThemisDB Security-Infrastruktur** auf:

**Wiederverwendung:**
- ✅ PluginSecurityVerifier für Signaturprüfung
- ✅ CMSSigningService für CMS/PKCS#7 Signaturen
- ✅ PKIKeyProvider für Schlüsselverwaltung
- ✅ PluginManifest-Struktur als Basis
- ✅ RocksDB für Manifest-Datenbank

**Neue Komponenten:**
- 📋 ReleaseManifestGenerator (Build-Tool)
- 📋 ManifestDatabase (Runtime Service)
- 📋 HotReloadEngine (Update-Engine)
- 📋 GitHub Actions Workflow
- 📋 HTTP API Endpoints

**Nächste Schritte:**
1. ReleaseManifestGenerator implementieren
2. ManifestDatabase mit RocksDB integrieren
3. HotReloadEngine mit Rollback-Support
4. GitHub Actions Workflow einrichten
5. API Endpoints hinzufügen
6. End-to-End Tests

Dies ermöglicht **sichere, automatisierte Hot-Reloads** mit vollständiger Audit-Trail und Rollback-Fähigkeit.
