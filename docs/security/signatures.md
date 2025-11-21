# Security Signatures System

## Problem: Henne-Ei bei In-File-Hash
Ein Hash **in** der Datei (`mime_types.yaml`) verändert die Datei selbst → Hash wird ungültig (Selbstreferenz-Paradoxon).

## Lösung: Externe Signatur-Datenbank
- **RocksDB Column Family** `security_signatures` speichert Hashes von kritischen Ressourcen.
- **Key-Schema**: `security_sig:<resource_id>` (z.B. `security_sig:config/mime_types.yaml`)
- **Value**: JSON-Serialisierung von `SecuritySignature` Entity
- **Verification**: Vor dem Laden einer kritischen Ressource (Config, Schema) wird File-Hash berechnet und mit DB-Signatur verglichen.

## Entity: SecuritySignature
```cpp
struct SecuritySignature {
    std::string resource_id;    // z.B. "config/mime_types.yaml"
    std::string hash;            // SHA256 hex (64 chars)
    std::string algorithm;       // "sha256"
    uint64_t created_at;         // Unix timestamp
    std::string created_by;      // User/System (optional)
    std::string comment;         // z.B. "Initial config signature"
};
```

## RocksDB Storage
- **Column Family**: Dedizierter CF `security_signatures` (isoliert von Nutzdaten).
- **Key Format**: `security_sig:<resource_id>`
- **Value Format**: JSON (kompakt, auditierbar)
- **Indexing**: Liste aller Signaturen via Prefix-Scan.

## SecuritySignatureManager API
```cpp
class SecuritySignatureManager {
public:
    // CRUD
    bool storeSignature(const SecuritySignature& sig);
    std::optional<SecuritySignature> getSignature(const std::string& resource_id);
    bool deleteSignature(const std::string& resource_id);
    std::vector<SecuritySignature> listAllSignatures();
    
    // Verification
    bool verifyFile(const std::string& file_path, const std::string& resource_id);
    std::string computeFileHash(const std::string& file_path);
};
```

## MimeDetector Integration
**Alter Flow** (unsicher):
```
loadYamlConfig() → Parse YAML → Verify in-file hash → Load mappings
```

**Neuer Flow** (sicher):
```
loadYamlConfig(path):
  1. resource_id = normalizeResourceId(path)  // "config/mime_types.yaml"
  2. file_hash = computeFileHash(path)
  3. db_sig = SecuritySignatureManager::getSignature(resource_id)
  4. if db_sig.has_value():
       if db_sig->hash != file_hash:
         LOG_ERROR("Hash mismatch")
         config_verified_ = false
         return false  // ODER Warnung + Fortfahren je nach Policy
       else:
         config_verified_ = true
  5. Parse YAML (integrity-Block ignorieren oder entfernen)
  6. Load mappings
```

## HTTP Server CRUD API
### Endpoints
```
GET    /api/security/signatures               - Liste aller Signaturen
GET    /api/security/signatures/:resource_id  - Abrufen einer Signatur
POST   /api/security/signatures               - Neue Signatur erstellen/aktualisieren
DELETE /api/security/signatures/:resource_id  - Signatur löschen
POST   /api/security/verify/:resource_id      - Verifiziere Datei gegen DB-Hash
```

### Request/Response Beispiele
**POST /api/security/signatures**
```json
{
  "resource_id": "config/mime_types.yaml",
  "hash": "a3f4b2...",
  "algorithm": "sha256",
  "created_by": "admin",
  "comment": "Production config v1.2"
}
```
Response 201:
```json
{
  "status": "created",
  "resource_id": "config/mime_types.yaml",
  "created_at": 1732000000
}
```

**GET /api/security/verify/config%2Fmime_types.yaml**
Response 200:
```json
{
  "resource_id": "config/mime_types.yaml",
  "verified": true,
  "current_hash": "a3f4b2...",
  "stored_hash": "a3f4b2...",
  "algorithm": "sha256",
  "last_updated": 1732000000
}
```

## CLI Tool: init_mime_signature.py
```bash
python scripts/init_mime_signature.py --resource config/mime_types.yaml --server http://localhost:8080
```
- Berechnet SHA256 der Datei.
- Sendet POST an `/api/security/signatures`.
- Verifiziert mit GET.

## Bedrohungsmodell
| Bedrohung | Ohne Signatur | Mit externer Signatur |
|-----------|---------------|----------------------|
| Manipulation mime_types.yaml | Unerkannt | Hash-Mismatch → Ablehnung |
| Löschen Signatur-DB | Unerkannt | Fehlerlog + Policy (z.B. Reject) |
| Replay alter Config | Unerkannt | Timestamp-Check + Review |
| Hash-Collision | SHA256-resistent | SHA256-resistent |
| SQL-Injection via resource_id | N/A | Input-Validation (Pfad-Normalisierung) |

## Policies (Konfigurierbar)
```yaml
security:
  signature_policy:
    missing_signature: WARN | REJECT
    hash_mismatch: WARN | REJECT
    require_signatures_for:
      - config/*.yaml
      - schema/*.json
```

## Erweiterungen
1. **Asymmetrische Signaturen**: Zusätzliches Feld `ed25519_signature` mit Public-Key-Verifikation.
2. **Multi-Hash**: Speichere SHA256 + SHA3-512 für Defense-in-Depth.
3. **Audit-Log**: Jede Signatur-Änderung wird geloggt (Wer? Wann? Warum?).
4. **Automatische Rotation**: Warnung bei Signaturen älter als X Tage.
5. **Merkle-Tree**: Bei vielen Config-Dateien kombinierter Root-Hash.

## Implementation Details
### RocksDB Column Family Setup
In `RocksDBWrapper::open()`:
```cpp
std::vector<std::string> cf_names = {"default", "graph", "vector", "timeseries", "security_signatures"};
```

### File Hash Berechnung
```cpp
std::string SecuritySignatureManager::computeFileHash(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), 
                             std::istreambuf_iterator<char>());
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<unsigned char*>(buffer.data()), buffer.size(), digest);
    char hex[65];
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
        sprintf(&hex[i*2], "%02x", digest[i]);
    return std::string(hex);
}
```

### Normalisierung Resource-ID
```cpp
std::string normalizeResourceId(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path p(path);
    p = fs::weakly_canonical(p); // Löse .., symlinks
    return p.generic_string();   // Plattform-unabhängig
}
```

## Security Best Practices
- **Least Privilege**: Nur Admin-Rolle darf Signaturen schreiben.
- **Rate Limiting**: POST `/api/security/signatures` max 10/min.
- **Input Validation**: resource_id darf nur alphanumerisch + `/._-` enthalten.
- **Atomic Updates**: WriteBatch für Signatur + Audit-Log Entry.

## Workflow: Config-Update
1. Entwickler ändert `config/mime_types.yaml`.
2. Lokal: `python scripts/init_mime_signature.py --resource config/mime_types.yaml`
3. Script berechnet Hash, POST an lokalen Server → DB-Update.
4. CI/CD: Bei Deploy wird Signatur ebenfalls in Prod-DB aktualisiert.
5. Server startet → MimeDetector lädt Config → Verifikation erfolgreich.

## Fazit
Externe Signatur-Speicherung in RocksDB löst Henne-Ei-Problem, ermöglicht tamper-detection und bietet Foundation für erweiterte Sicherheitsmechanismen (Asymmetrische Signaturen, Audit-Logs, Policy-Enforcement).

---

# Content Policy System (Whitelist/Blacklist)

## Problem
Nicht nur MIME-Typen erkennen, sondern auch **Upload-Validierung**: Welche Dateitypen sind erlaubt? Welche Größenlimits gelten?

## Lösung: Policy-Sektion in mime_types.yaml
Die selbe Datei enthält sowohl MIME-Detection-Regeln ALS AUCH Upload-Policies. Vorteil: **Single Source of Truth** + **eine Signatur deckt beide ab**.

## YAML Schema Extension
```yaml
# External signature via DB (see SECURITY_SIGNATURES system)

policies:
  default_max_size: 104857600  # 100 MB
  default_action: allow         # allow | deny
  
  allowed:
    - mime_type: "text/plain"
      max_size: 10485760         # 10 MB
      description: "Plain text files"
    - mime_type: "application/geo+json"
      max_size: 524288000        # 500 MB
      description: "GeoJSON spatial data"
    - mime_type: "application/vnd.themis.vpb+json"
      max_size: 1073741824       # 1 GB
      description: "Themis VPB building data"
    - mime_type: "application/x-parquet"
      max_size: 2147483648       # 2 GB
      description: "Parquet analytics data"
    - mime_type: "application/zip"
      max_size: 1073741824       # 1 GB
      description: "ZIP archives"
  
  denied:
    - mime_type: "application/x-msdownload"
      description: "Windows executables"
      reason: "Security risk - executable files not allowed"
    - mime_type: "application/x-executable"
      description: "Linux/Unix executables"
      reason: "Security risk - executable files not allowed"
    - mime_type: "application/javascript"
      description: "JavaScript code"
      reason: "Security risk - active scripts not allowed"
    - mime_type: "text/html"
      description: "HTML documents"
      reason: "Security risk - potential XSS vectors"
  
  category_rules:
    executable:
      action: deny
      reason: "Executable files pose security risks"
    geo:
      action: allow
      max_size: 1073741824  # 1 GB for geo category
    themis:
      action: allow
      max_size: 2147483648  # 2 GB for themis formats
    binary_data:
      action: allow
      max_size: 5368709120  # 5 GB for binary analytics
```

## ContentPolicy Entity
```cpp
struct MimePolicy {
    std::string mime_type;
    uint64_t max_size;
    std::string description;
    std::string reason;  // For denied entries
};

struct CategoryPolicy {
    std::string category;
    bool action;  // true = allow, false = deny
    uint64_t max_size;
    std::string reason;
};

struct ContentPolicy {
    uint64_t default_max_size;
    bool default_action;
    std::vector<MimePolicy> allowed;
    std::vector<MimePolicy> denied;
    std::map<std::string, CategoryPolicy> category_rules;
    
    bool isAllowed(const std::string& mime_type) const;
    bool isDenied(const std::string& mime_type) const;
    uint64_t getMaxSize(const std::string& mime_type) const;
    uint64_t getCategoryMaxSize(const std::string& category) const;
    std::string getDenialReason(const std::string& mime_type) const;
};

struct ValidationResult {
    bool allowed;
    std::string mime_type;
    uint64_t file_size;
    uint64_t max_allowed_size;
    std::string reason;
    
    // Detailed flags
    bool size_exceeded = false;
    bool blacklisted = false;
    bool not_whitelisted = false;
};
```

## MimeDetector Integration
```cpp
class MimeDetector {
    ContentPolicy policy_;  // Loaded from YAML
    
public:
    ValidationResult validateUpload(const std::string& filename, 
                                     uint64_t file_size) const;
};
```

### Validation Logic
```cpp
ValidationResult MimeDetector::validateUpload(filename, file_size) {
    ValidationResult result;
    result.mime_type = fromExtension(filename);
    result.file_size = file_size;
    
    // Step 1: Check blacklist
    if (policy_.isDenied(result.mime_type)) {
        result.allowed = false;
        result.blacklisted = true;
        result.reason = policy_.getDenialReason(result.mime_type);
        return result;
    }
    
    // Step 2: Check whitelist
    if (policy_.isAllowed(result.mime_type)) {
        uint64_t max_size = policy_.getMaxSize(result.mime_type);
        result.max_allowed_size = max_size;
        
        if (file_size > max_size) {
            result.allowed = false;
            result.size_exceeded = true;
            result.reason = "File size exceeds limit";
            return result;
        }
        
        result.allowed = true;
        result.reason = "Allowed by whitelist";
        return result;
    }
    
    // Step 3: Check category rules
    for (auto& [category, mime_set] : categories_) {
        if (mime_set.count(result.mime_type) > 0) {
            CategoryPolicy cat_policy = policy_.category_rules[category];
            if (!cat_policy.action) {
                result.allowed = false;
                result.blacklisted = true;
                result.reason = "Category denied: " + cat_policy.reason;
                return result;
            }
            
            result.max_allowed_size = cat_policy.max_size;
            if (file_size > cat_policy.max_size) {
                result.allowed = false;
                result.size_exceeded = true;
                result.reason = "Exceeds category size limit";
                return result;
            }
            
            result.allowed = true;
            result.reason = "Allowed by category '" + category + "'";
            return result;
        }
    }
    
    // Step 4: Apply default policy
    if (policy_.default_action) {
        result.max_allowed_size = policy_.default_max_size;
        if (file_size > policy_.default_max_size) {
            result.allowed = false;
            result.size_exceeded = true;
            result.reason = "Exceeds default size limit";
            return result;
        }
        result.allowed = true;
        result.reason = "Allowed by default policy";
        return result;
    } else {
        result.allowed = false;
        result.not_whitelisted = true;
        result.reason = "Not in whitelist and default policy is deny";
        return result;
    }
}
```

## HTTP API: Content Validation
### Endpoint
```
POST /api/content/validate
```

### Request
```json
{
  "filename": "map.geojson",
  "file_size": 104857600
}
```

### Response (200 OK - Allowed)
```json
{
  "allowed": true,
  "filename": "map.geojson",
  "mime_type": "application/geo+json",
  "file_size": 104857600,
  "max_allowed_size": 524288000,
  "reason": "Allowed by whitelist"
}
```

### Response (403 Forbidden - Denied)
```json
{
  "allowed": false,
  "filename": "malware.exe",
  "mime_type": "application/x-msdownload",
  "file_size": 1024,
  "max_allowed_size": 0,
  "reason": "Security risk - executable files not allowed",
  "blacklisted": true,
  "size_exceeded": false,
  "not_whitelisted": false
}
```

## Integration in File Upload Flow
**Alter Flow**:
```
POST /api/content → Parse body → Write to DB
```

**Neuer Flow** (mit Validation):
```
POST /api/content:
  1. Parse body (extract filename, size from multipart or metadata)
  2. ValidationResult result = mime_detector_->validateUpload(filename, size)
  3. if (!result.allowed):
       LOG_WARN("Upload denied: {}", result.reason)
       return 403 Forbidden + JSON error
  4. Proceed with actual content storage
  5. LOG_INFO("Upload accepted: {} ({} bytes)", filename, size)
```

## Security Model
- **Policy protected by signature**: `mime_types.yaml` enthält sowohl MIME-Regeln ALS AUCH Policies → eine DB-Signatur schützt beide.
- **Tamper-Detection**: Änderungen an Whitelist/Blacklist erfordern Hash-Update in DB.
- **Auditierbar**: Policy-Änderungen erfordern Admin-Zugriff + Signatur-Update (geloggt).

## Use Cases
| Scenario | Config | Result |
|----------|--------|--------|
| Text file 1MB | `text/plain` whitelist, 10MB limit | ✅ Allowed |
| Text file 20MB | `text/plain` whitelist, 10MB limit | ❌ Size exceeded |
| GeoJSON 100MB | `application/geo+json` whitelist, 500MB limit | ✅ Allowed |
| Executable .exe | `application/x-msdownload` blacklist | ❌ Blacklisted (security risk) |
| JavaScript .js | `application/javascript` blacklist | ❌ Blacklisted (security risk) |
| HTML .html | `text/html` blacklist | ❌ Blacklisted (XSS risk) |
| Unknown .xyz 50MB | No whitelist entry, default 100MB | ✅ Allowed by default |
| Unknown .xyz 150MB | No whitelist entry, default 100MB | ❌ Exceeds default limit |
| Parquet 1.5GB | `application/x-parquet` whitelist, 2GB limit | ✅ Allowed (analytics data) |
| Themis VPB 500MB | `application/vnd.themis.vpb+json` whitelist, 1GB limit | ✅ Allowed (building data) |

## Testing
**Script**: `test_content_policy.ps1`
```powershell
# Test 1: Allowed text file
Invoke-WebRequest -Uri http://localhost:8080/api/content/validate -Method POST -Body '{"filename":"doc.txt","file_size":1048576}'

# Test 2: Blacklisted executable
Invoke-WebRequest -Uri http://localhost:8080/api/content/validate -Method POST -Body '{"filename":"malware.exe","file_size":1024}'

# Test 3: Size exceeded
Invoke-WebRequest -Uri http://localhost:8080/api/content/validate -Method POST -Body '{"filename":"huge.txt","file_size":20971520}'
```

## Benefits
1. **Defense-in-Depth**: Whitelist + Blacklist + Size Limits.
2. **Flexible Policies**: Per-MIME, per-category, default fallback.
3. **Single Source of Truth**: MIME detection + policies in one YAML.
4. **Tamper-Proof**: External signature covers entire config.
5. **Pre-Upload Validation**: Client can check BEFORE sending large files.
6. **Auditability**: All policy changes require DB signature update (logged).

## Future Extensions
1. **User-Role Policies**: Admin kann größere Files als normale User.
2. **Dynamic Policies**: Load policies from DB instead of YAML (runtime updates).
3. **Policy Versions**: Track history of policy changes (Git-like diffs).
4. **Content Scanning Integration**: Integrate with ClamAV for malware scanning.
5. **Regex-based MIME matching**: `video/*` statt einzelne MIME-Typen.

