# Externe Blob-Storage-Analyse für ThemisDB

**Datum:** 21. November 2025  
**Anforderung:** Support für externe Blob-Storage (ActiveDirectory, AWS S3, etc.)  
**Status:** 🟡 Design vorhanden, Implementation ausstehend

---

## 📋 Anforderung (neue Prüfung)

> Es ist im Prinzip vorgesehen Dokumente als Binärblob in der RocksDB zu speichern. 
> Allerdings soll Themis auch damit umgehen können das die Dateien irgendwo in einen 
> ActiveDirectory, AWS S3 und ähnliches liegen.

---

## 🔍 Aktueller Stand

### ✅ Was existiert bereits

#### 1. Design-Dokumentation
**Dateien:**
- `docs/content_architecture.md` (Zeilen 436-448)
- `docs/content_pipeline.md` (Zeile 217)
- `src/server/VCCDB Design.md`

**Strategie dokumentiert:**
```cpp
// Aus docs/content_architecture.md
struct BlobStorageConfig {
    int64_t inline_threshold_bytes = 1024 * 1024; // 1 MB
    std::string external_storage_path = "./data/blobs/";
};

// In ContentManager::ingestContent()
if (blob.size() < config.inline_threshold_bytes) {
    // Store inline in RocksDB (< 1 MB)
    entity.setBlob(blob);
} else {
    // Store externally (filesystem or S3)
    std::string blob_path = external_storage_path + content_id + ".blob";
    writeToFile(blob_path, blob);
    entity.set("blob_ref", blob_path);
}
```

#### 2. Datenmodell vorbereitet
**Datei:** `include/content/content_manager.h` (Zeile 82)

```cpp
struct ChunkMeta {
    std::string id;
    std::string content_id;
    // ...
    std::string blob_ref;  // ✅ Reference to blob storage (for binary chunks)
    // ...
};
```

#### 3. RocksDB BlobDB-Support
**Datei:** `src/server/http_server.cpp`

```cpp
{
    "enable_blobdb", storage_->getConfig().enable_blobdb,
    "blob_size_threshold", storage_->getConfig().blob_size_threshold
}
```

**RocksDB BlobDB:**
- Automatische Extraktion großer Values aus LSM-Tree
- Separate Blob-Dateien für Werte > Threshold
- Transparente Integration (keine API-Änderungen)

---

### ❌ Was fehlt

#### 1. IBlobStorageBackend Interface
**Status:** ❌ Nicht implementiert

**Benötigt:**
```cpp
// include/storage/blob_storage_backend.h
namespace themis {
namespace storage {

enum class BlobStorageType {
    INLINE,       // RocksDB inline (< 1 MB)
    ROCKSDB_BLOB, // RocksDB BlobDB (1-10 MB)
    FILESYSTEM,   // Lokales Filesystem
    S3,           // AWS S3
    AZURE_BLOB,   // Azure Blob Storage
    GCS,          // Google Cloud Storage
    WEBDAV,       // WebDAV (für ActiveDirectory-Integration)
    CUSTOM        // User-defined Backend
};

struct BlobRef {
    std::string id;           // Blob-ID (UUID)
    BlobStorageType type;     // Storage-Backend
    std::string uri;          // Backend-spezifischer URI
    int64_t size_bytes;       // Original-Größe
    std::string hash_sha256;  // Content-Hash
};

class IBlobStorageBackend {
public:
    virtual ~IBlobStorageBackend() = default;
    
    // Blob schreiben
    virtual BlobRef put(
        const std::string& blob_id,
        const std::vector<uint8_t>& data
    ) = 0;
    
    // Blob lesen
    virtual std::optional<std::vector<uint8_t>> get(
        const BlobRef& ref
    ) = 0;
    
    // Blob löschen
    virtual bool remove(const BlobRef& ref) = 0;
    
    // Blob existiert?
    virtual bool exists(const BlobRef& ref) = 0;
    
    // Backend-Name
    virtual std::string name() const = 0;
    
    // Backend ist verfügbar?
    virtual bool isAvailable() const = 0;
};

} } // namespace themis::storage
```

---

#### 2. Filesystem Backend
**Status:** ❌ Nicht implementiert

**Benötigt:**
```cpp
// src/storage/blob_backend_filesystem.cpp
class FilesystemBlobBackend : public IBlobStorageBackend {
private:
    std::string base_path_;  // z.B. "./data/blobs/"
    
public:
    FilesystemBlobBackend(const std::string& base_path)
        : base_path_(base_path) {
        // Verzeichnis erstellen falls nicht vorhanden
        std::filesystem::create_directories(base_path_);
    }
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        // Hierarchisches Verzeichnis (blob_id[:2]/blob_id[2:4]/blob_id)
        std::string prefix = blob_id.substr(0, 2);
        std::string subdir = blob_id.substr(2, 2);
        std::string dir_path = base_path_ + "/" + prefix + "/" + subdir;
        std::filesystem::create_directories(dir_path);
        
        std::string file_path = dir_path + "/" + blob_id + ".blob";
        std::ofstream ofs(file_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(data.data()), data.size());
        
        BlobRef ref;
        ref.id = blob_id;
        ref.type = BlobStorageType::FILESYSTEM;
        ref.uri = file_path;
        ref.size_bytes = data.size();
        ref.hash_sha256 = sha256(data);
        return ref;
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        std::ifstream ifs(ref.uri, std::ios::binary);
        if (!ifs) return std::nullopt;
        
        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>()
        );
        return data;
    }
    
    bool remove(const BlobRef& ref) override {
        return std::filesystem::remove(ref.uri);
    }
    
    bool exists(const BlobRef& ref) override {
        return std::filesystem::exists(ref.uri);
    }
    
    std::string name() const override { return "filesystem"; }
    bool isAvailable() const override { return true; }
};
```

---

#### 3. S3 Backend
**Status:** ❌ Nicht implementiert (aber dokumentiert in NEXT_STEPS_ANALYSIS.md)

**Abhängigkeit:** `aws-sdk-cpp` (bereits in Dokumentation erwähnt)

**Benötigt:**
```cpp
// src/storage/blob_backend_s3.cpp
#include <aws/s3/S3Client.h>
#include <aws/s3/model/PutObjectRequest.h>
#include <aws/s3/model/GetObjectRequest.h>

class S3BlobBackend : public IBlobStorageBackend {
private:
    std::shared_ptr<Aws::S3::S3Client> s3_client_;
    std::string bucket_name_;
    std::string prefix_;  // z.B. "themis-blobs/"
    
public:
    S3BlobBackend(
        const std::string& bucket_name,
        const std::string& region = "us-east-1",
        const std::string& prefix = ""
    ) : bucket_name_(bucket_name), prefix_(prefix) {
        Aws::Client::ClientConfiguration config;
        config.region = region;
        s3_client_ = std::make_shared<Aws::S3::S3Client>(config);
    }
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        std::string key = prefix_ + blob_id;
        
        Aws::S3::Model::PutObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(key);
        
        auto stream = std::make_shared<Aws::StringStream>();
        stream->write(reinterpret_cast<const char*>(data.data()), data.size());
        request.SetBody(stream);
        
        auto outcome = s3_client_->PutObject(request);
        if (!outcome.IsSuccess()) {
            throw std::runtime_error("S3 PutObject failed: " + 
                outcome.GetError().GetMessage());
        }
        
        BlobRef ref;
        ref.id = blob_id;
        ref.type = BlobStorageType::S3;
        ref.uri = "s3://" + bucket_name_ + "/" + key;
        ref.size_bytes = data.size();
        ref.hash_sha256 = sha256(data);
        return ref;
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        // Parse S3 URI
        std::string key = parseS3Key(ref.uri);
        
        Aws::S3::Model::GetObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(key);
        
        auto outcome = s3_client_->GetObject(request);
        if (!outcome.IsSuccess()) {
            return std::nullopt;
        }
        
        auto& stream = outcome.GetResult().GetBody();
        std::vector<uint8_t> data(
            (std::istreambuf_iterator<char>(stream)),
            std::istreambuf_iterator<char>()
        );
        return data;
    }
    
    bool remove(const BlobRef& ref) override {
        std::string key = parseS3Key(ref.uri);
        
        Aws::S3::Model::DeleteObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(key);
        
        auto outcome = s3_client_->DeleteObject(request);
        return outcome.IsSuccess();
    }
    
    bool exists(const BlobRef& ref) override {
        std::string key = parseS3Key(ref.uri);
        
        Aws::S3::Model::HeadObjectRequest request;
        request.SetBucket(bucket_name_);
        request.SetKey(key);
        
        auto outcome = s3_client_->HeadObject(request);
        return outcome.IsSuccess();
    }
    
    std::string name() const override { return "s3"; }
    bool isAvailable() const override {
        // Test mit ListBuckets
        auto outcome = s3_client_->ListBuckets();
        return outcome.IsSuccess();
    }
};
```

---

#### 4. Azure Blob Backend
**Status:** ❌ Nicht implementiert

**Abhängigkeit:** `azure-storage-blobs-cpp`

**Benötigt:**
```cpp
// src/storage/blob_backend_azure.cpp
#include <azure/storage/blobs.hpp>

class AzureBlobBackend : public IBlobStorageBackend {
private:
    std::shared_ptr<Azure::Storage::Blobs::BlobContainerClient> container_;
    std::string container_name_;
    
public:
    AzureBlobBackend(
        const std::string& connection_string,
        const std::string& container_name
    ) : container_name_(container_name) {
        auto client = Azure::Storage::Blobs::BlobServiceClient::CreateFromConnectionString(
            connection_string
        );
        container_ = std::make_shared<Azure::Storage::Blobs::BlobContainerClient>(
            client.GetBlobContainerClient(container_name_)
        );
        container_->CreateIfNotExists();
    }
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        auto blob_client = container_->GetBlockBlobClient(blob_id);
        
        Azure::Core::IO::MemoryBodyStream stream(data.data(), data.size());
        blob_client.UploadFrom(stream);
        
        BlobRef ref;
        ref.id = blob_id;
        ref.type = BlobStorageType::AZURE_BLOB;
        ref.uri = "https://" + container_name_ + ".blob.core.windows.net/" + blob_id;
        ref.size_bytes = data.size();
        ref.hash_sha256 = sha256(data);
        return ref;
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        auto blob_client = container_->GetBlockBlobClient(ref.id);
        
        try {
            auto download_result = blob_client.Download();
            auto& stream = download_result.Value.BodyStream;
            
            std::vector<uint8_t> data;
            data.resize(download_result.Value.BlobSize);
            stream->Read(data.data(), data.size());
            return data;
        } catch (...) {
            return std::nullopt;
        }
    }
    
    bool remove(const BlobRef& ref) override {
        auto blob_client = container_->GetBlockBlobClient(ref.id);
        try {
            blob_client.Delete();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    bool exists(const BlobRef& ref) override {
        auto blob_client = container_->GetBlockBlobClient(ref.id);
        try {
            blob_client.GetProperties();
            return true;
        } catch (...) {
            return false;
        }
    }
    
    std::string name() const override { return "azure_blob"; }
    bool isAvailable() const override {
        try {
            container_->GetProperties();
            return true;
        } catch (...) {
            return false;
        }
    }
};
```

---

#### 5. WebDAV Backend (für ActiveDirectory/SharePoint)
**Status:** ❌ Nicht dokumentiert/implementiert

**Abhängigkeit:** `libcurl` (bereits im Projekt)

**Benötigt:**
```cpp
// src/storage/blob_backend_webdav.cpp
#include <curl/curl.h>

class WebDAVBlobBackend : public IBlobStorageBackend {
private:
    std::string base_url_;       // z.B. "https://sharepoint.company.com/sites/themis/"
    std::string username_;
    std::string password_;
    CURL* curl_;
    
public:
    WebDAVBlobBackend(
        const std::string& base_url,
        const std::string& username,
        const std::string& password
    ) : base_url_(base_url), username_(username), password_(password) {
        curl_ = curl_easy_init();
        curl_easy_setopt(curl_, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl_, CURLOPT_USERNAME, username_.c_str());
        curl_easy_setopt(curl_, CURLOPT_PASSWORD, password_.c_str());
    }
    
    ~WebDAVBlobBackend() {
        if (curl_) curl_easy_cleanup(curl_);
    }
    
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) override {
        std::string url = base_url_ + "/" + blob_id + ".blob";
        
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_, CURLOPT_UPLOAD, 1L);
        
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);
        
        // Body-Daten
        FILE* tmp = tmpfile();
        fwrite(data.data(), 1, data.size(), tmp);
        rewind(tmp);
        curl_easy_setopt(curl_, CURLOPT_READDATA, tmp);
        curl_easy_setopt(curl_, CURLOPT_INFILESIZE_LARGE, (curl_off_t)data.size());
        
        CURLcode res = curl_easy_perform(curl_);
        fclose(tmp);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            throw std::runtime_error("WebDAV PUT failed: " + 
                std::string(curl_easy_strerror(res)));
        }
        
        BlobRef ref;
        ref.id = blob_id;
        ref.type = BlobStorageType::WEBDAV;
        ref.uri = url;
        ref.size_bytes = data.size();
        ref.hash_sha256 = sha256(data);
        return ref;
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) override {
        curl_easy_setopt(curl_, CURLOPT_URL, ref.uri.c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPGET, 1L);
        
        std::vector<uint8_t> data;
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, 
            +[](void* ptr, size_t size, size_t nmemb, void* userdata) -> size_t {
                auto* vec = static_cast<std::vector<uint8_t>*>(userdata);
                size_t total = size * nmemb;
                vec->insert(vec->end(), (uint8_t*)ptr, (uint8_t*)ptr + total);
                return total;
            });
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &data);
        
        CURLcode res = curl_easy_perform(curl_);
        if (res != CURLE_OK) {
            return std::nullopt;
        }
        
        return data;
    }
    
    bool remove(const BlobRef& ref) override {
        curl_easy_setopt(curl_, CURLOPT_URL, ref.uri.c_str());
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "DELETE");
        
        CURLcode res = curl_easy_perform(curl_);
        return res == CURLE_OK;
    }
    
    bool exists(const BlobRef& ref) override {
        curl_easy_setopt(curl_, CURLOPT_URL, ref.uri.c_str());
        curl_easy_setopt(curl_, CURLOPT_NOBODY, 1L);  // HEAD request
        
        CURLcode res = curl_easy_perform(curl_);
        
        long response_code;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &response_code);
        
        return res == CURLE_OK && response_code == 200;
    }
    
    std::string name() const override { return "webdav"; }
    bool isAvailable() const override {
        // Test mit PROPFIND auf base_url
        curl_easy_setopt(curl_, CURLOPT_URL, base_url_.c_str());
        curl_easy_setopt(curl_, CURLOPT_CUSTOMREQUEST, "PROPFIND");
        
        CURLcode res = curl_easy_perform(curl_);
        return res == CURLE_OK;
    }
};
```

---

#### 6. BlobStorageManager (Orchestrator)
**Status:** ❌ Nicht implementiert

**Benötigt:**
```cpp
// include/storage/blob_storage_manager.h
class BlobStorageManager {
private:
    std::unordered_map<BlobStorageType, std::shared_ptr<IBlobStorageBackend>> backends_;
    BlobStorageConfig config_;
    
public:
    BlobStorageManager(const BlobStorageConfig& config) : config_(config) {}
    
    void registerBackend(BlobStorageType type, std::shared_ptr<IBlobStorageBackend> backend) {
        backends_[type] = backend;
    }
    
    // Automatische Backend-Auswahl basierend auf Größe
    BlobRef put(const std::string& blob_id, const std::vector<uint8_t>& data) {
        BlobStorageType target_type;
        
        if (data.size() < config_.inline_threshold_bytes) {
            target_type = BlobStorageType::INLINE;
        } else if (data.size() < config_.rocksdb_blob_threshold_bytes) {
            target_type = BlobStorageType::ROCKSDB_BLOB;
        } else if (config_.prefer_s3) {
            target_type = BlobStorageType::S3;
        } else {
            target_type = BlobStorageType::FILESYSTEM;
        }
        
        auto backend = backends_[target_type];
        if (!backend || !backend->isAvailable()) {
            // Fallback zu Filesystem
            backend = backends_[BlobStorageType::FILESYSTEM];
        }
        
        return backend->put(blob_id, data);
    }
    
    std::optional<std::vector<uint8_t>> get(const BlobRef& ref) {
        auto backend = backends_[ref.type];
        if (!backend) {
            throw std::runtime_error("Backend not registered: " + 
                std::to_string(static_cast<int>(ref.type)));
        }
        return backend->get(ref);
    }
};
```

---

#### 7. Konfiguration
**Status:** ❌ Nicht implementiert

**Benötigt in `config.yaml`:**
```yaml
blob_storage:
  # Threshold für inline Storage in RocksDB
  inline_threshold_bytes: 1048576  # 1 MB
  
  # Threshold für RocksDB BlobDB (automatisch)
  rocksdb_blob_threshold_bytes: 10485760  # 10 MB
  
  # Backends (priorisiert nach Reihenfolge)
  backends:
    - type: filesystem
      enabled: true
      config:
        base_path: "./data/blobs"
    
    - type: s3
      enabled: false
      config:
        bucket: "themis-production-blobs"
        region: "eu-central-1"
        prefix: "blobs/"
        credentials:
          access_key_id: "${AWS_ACCESS_KEY_ID}"
          secret_access_key: "${AWS_SECRET_ACCESS_KEY}"
    
    - type: azure_blob
      enabled: false
      config:
        connection_string: "${AZURE_STORAGE_CONNECTION_STRING}"
        container: "themis-blobs"
    
    - type: webdav
      enabled: false
      config:
        base_url: "https://sharepoint.company.com/sites/themis/blobs"
        username: "${WEBDAV_USER}"
        password: "${WEBDAV_PASSWORD}"
```

---

## 🎯 Implementierungsplan

### Phase 1: Interface & Filesystem (1 Woche)
**Priorität:** 🔴 HOCH

1. **IBlobStorageBackend Interface** (1 Tag)
   - Datei: `include/storage/blob_storage_backend.h`
   - BlobRef Struktur
   - Interface-Definition

2. **FilesystemBlobBackend** (2 Tage)
   - Datei: `src/storage/blob_backend_filesystem.cpp`
   - Hierarchische Verzeichnisstruktur
   - Tests: `tests/test_blob_filesystem.cpp`

3. **BlobStorageManager** (2 Tage)
   - Datei: `include/storage/blob_storage_manager.h`
   - Backend-Registry
   - Automatische Threshold-basierte Auswahl
   - Tests: `tests/test_blob_storage_manager.cpp`

4. **Integration in ContentManager** (1 Tag)
   - Ersetze direkte RocksDB-Aufrufe
   - Nutze BlobStorageManager

---

### Phase 2: Cloud Backends (2 Wochen)
**Priorität:** 🟡 MEDIUM

1. **S3Backend** (1 Woche)
   - Dependency: `aws-sdk-cpp` zu vcpkg.json hinzufügen
   - Implementation: `src/storage/blob_backend_s3.cpp`
   - Multipart-Upload für große Blobs (> 100 MB)
   - Tests: `tests/test_blob_s3.cpp` (mit MinIO lokal)

2. **AzureBlobBackend** (3 Tage)
   - Dependency: `azure-storage-blobs-cpp`
   - Implementation: `src/storage/blob_backend_azure.cpp`
   - Tests: `tests/test_blob_azure.cpp`

3. **WebDAVBackend** (2 Tage)
   - Nutzt bestehendes libcurl
   - Implementation: `src/storage/blob_backend_webdav.cpp`
   - Tests: `tests/test_blob_webdav.cpp`

---

### Phase 3: Konfiguration & Dokumentation (3 Tage)
**Priorität:** 🟡 MEDIUM

1. **YAML-Konfiguration** (1 Tag)
   - Erweitere `ServerConfig`
   - Parser für `blob_storage` Section

2. **Dokumentation** (2 Tage)
   - Guide: `docs/blob_storage_backends.md`
   - Beispiele für S3, Azure, WebDAV, ActiveDirectory
   - Migration-Guide (RocksDB → External Storage)

---

## 📊 Vergleich: Blob-Storage-Backends

| Backend | Geschwindigkeit | Skalierbarkeit | Kosten | Komplexität | Use Case |
|---------|----------------|----------------|--------|-------------|----------|
| **Inline (RocksDB)** | ⚡⚡⚡ Sehr schnell | ⚠️ Begrenzt (GB-Bereich) | 💰 Gratis | ✅ Einfach | < 1 MB Metadaten |
| **RocksDB BlobDB** | ⚡⚡ Schnell | ⚠️ Mittel (100 GB) | 💰 Gratis | ✅ Einfach | 1-10 MB Blobs |
| **Filesystem** | ⚡⚡ Schnell | ⚠️ Mittel (TB-Bereich) | 💰 SSD-Kosten | ✅ Einfach | 10 MB - 1 GB |
| **S3** | ⚡ Mittel | ✅ Unbegrenzt | 💰💰 $0.023/GB | ⚠️ Mittel | > 1 GB, Archiv |
| **Azure Blob** | ⚡ Mittel | ✅ Unbegrenzt | 💰💰 $0.018/GB | ⚠️ Mittel | > 1 GB, Archiv |
| **WebDAV** | ⚠️ Langsam | ⚠️ Mittel | 💰 SharePoint-Lizenz | ⚠️⚠️ Komplex | Enterprise-Integration |

---

## 🔒 Sicherheits-Überlegungen

### 1. Authentifizierung
- **S3:** IAM Roles (empfohlen) oder Access Keys
- **Azure:** Connection Strings oder Managed Identity
- **WebDAV:** Basic Auth (HTTPS erforderlich!)
- **ActiveDirectory:** Kerberos/NTLM über WebDAV

### 2. Verschlüsselung
- **At Rest:** 
  - S3: SSE-S3, SSE-KMS, SSE-C
  - Azure: AES-256
  - Filesystem: LUKS, BitLocker
  
- **In Transit:**
  - Alle Backends MÜSSEN HTTPS/TLS nutzen
  - Certificate Pinning für kritische Umgebungen

### 3. Zugriffskontrolle
- Blob-Refs in RocksDB enthalten KEINE Credentials
- Credentials NUR über Environment Variables oder Vault
- Least-Privilege: Nur `PutObject`, `GetObject`, `DeleteObject`

---

## 🧪 Testing-Strategie

### Unit Tests
```cpp
// tests/test_blob_storage_manager.cpp
TEST(BlobStorageManager, AutomaticBackendSelection) {
    BlobStorageConfig config;
    config.inline_threshold_bytes = 1024;  // 1 KB
    
    BlobStorageManager manager(config);
    
    // Kleiner Blob → Inline
    auto small_ref = manager.put("small", std::vector<uint8_t>(512));
    EXPECT_EQ(small_ref.type, BlobStorageType::INLINE);
    
    // Großer Blob → Filesystem
    auto large_ref = manager.put("large", std::vector<uint8_t>(2048));
    EXPECT_EQ(large_ref.type, BlobStorageType::FILESYSTEM);
}
```

### Integration Tests
```cpp
// tests/test_blob_s3_integration.cpp
TEST(S3Backend, RoundTrip) {
    // Verwendet MinIO Docker-Container
    S3BlobBackend backend("test-bucket", "us-east-1");
    
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto ref = backend.put("test-blob", data);
    
    auto retrieved = backend.get(ref);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(*retrieved, data);
}
```

---

## 📋 Zusammenfassung

### ✅ Positiv
1. **Design ist durchdacht:** Threshold-basierte Strategie sinnvoll
2. **Datenmodell vorbereitet:** `blob_ref` Feld existiert
3. **RocksDB BlobDB:** Transparente Integration für 1-10 MB Blobs
4. **Dokumentation vorhanden:** Strategie in mehreren Docs beschrieben

### ❌ Negativ
1. **Keine Implementation:** Alle Backends fehlen (Filesystem, S3, Azure, WebDAV)
2. **Kein Interface:** IBlobStorageBackend muss erstellt werden
3. **Keine Konfiguration:** blob_storage Section fehlt in config.yaml
4. **ActiveDirectory nicht dokumentiert:** WebDAV-Ansatz nicht erwähnt

### 🎯 Empfehlung
**Phase 1 (1 Woche) umsetzen:**
- Filesystem-Backend ist schnell implementiert
- Löst 80% der Use Cases
- S3/Azure/WebDAV als spätere Erweiterung

**Aufwand gesamt:** 3-4 Wochen für alle Backends

---

**Erstellt:** 21. November 2025  
**Status:** ✅ Analyse abgeschlossen  
**Nächste Schritte:** Phase 1 Implementation starten
