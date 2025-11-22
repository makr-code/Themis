# Manifest Security - Authentizität der Binärdateien sicherstellen

## Kernproblem

**Wie stellen wir sicher, dass heruntergeladene Binärdateien wirklich von uns (ThemisDB Team) stammen?**

## Das Problem in der Praxis

```
Angreifer-Szenario:
1. User lädt "themis_server" von github.com/makr-code/ThemisDB/releases
2. ABER: Angreifer hat sich in GitHub eingehackt
3. ODER: Angreifer macht Man-in-the-Middle Attack
4. ODER: Angreifer kompromittiert GitHub CDN
5. User installiert manipulierte Binary
6. ❌ System kompromittiert
```

## Lösung: Signiertes Manifest

### Prinzip

```
┌─────────────────────────────────────────────────┐
│  Build-Server (Sicher, nur ThemisDB Team)       │
├─────────────────────────────────────────────────┤
│                                                 │
│  1. Build Binary:                               │
│     themis_server (Binary)                      │
│                                                 │
│  2. Calculate Hash:                             │
│     SHA256(themis_server) = abc123...           │
│                                                 │
│  3. Create Manifest:                            │
│     manifest.json:                              │
│     {                                           │
│       "files": [{                               │
│         "path": "themis_server",                │
│         "sha256": "abc123..."                   │
│       }]                                        │
│     }                                           │
│                                                 │
│  4. Sign Manifest with PRIVATE KEY:             │
│     signature = Sign(manifest, private_key)     │
│                                                 │
│  5. Embed Signature:                            │
│     manifest.json["signature"] = signature      │
│     manifest.json["certificate"] = public_cert  │
│                                                 │
└─────────────────────────────────────────────────┘
                    │
                    │ Upload to GitHub
                    ▼
┌─────────────────────────────────────────────────┐
│  GitHub Release (Öffentlich)                    │
├─────────────────────────────────────────────────┤
│  - manifest.json (signiert)                     │
│  - themis_server (binary)                       │
│  - themis_core.so (binary)                      │
└─────────────────────────────────────────────────┘
                    │
                    │ Download
                    ▼
┌─────────────────────────────────────────────────┐
│  User Installation                               │
├─────────────────────────────────────────────────┤
│                                                 │
│  1. Download manifest.json                      │
│                                                 │
│  2. Verify Signature:                           │
│     ✅ Signature valid?                         │
│     ✅ Certificate from ThemisDB Team?          │
│     ✅ Certificate not revoked?                 │
│                                                 │
│  3. Download themis_server                      │
│                                                 │
│  4. Verify Hash:                                │
│     calculated = SHA256(themis_server)          │
│     expected = manifest["files"][0]["sha256"]   │
│     ✅ calculated == expected?                  │
│                                                 │
│  5. ONLY IF ALL CHECKS PASS:                    │
│     Install and run themis_server               │
│                                                 │
└─────────────────────────────────────────────────┘
```

## Was das Manifest garantiert

### ✅ Authentizität

**Frage:** Kommt die Binary wirklich vom ThemisDB Team?

**Antwort:** Ja, durch digitale Signatur!

```cpp
// Manifest ist signiert mit privatem Schlüssel des ThemisDB Teams
// Nur wir haben den privaten Schlüssel
// Signatur kann mit öffentlichem Zertifikat verifiziert werden

bool isAuthentic = verifySignature(
    manifest,
    manifest.signature,
    themisdb_team_certificate
);

if (!isAuthentic) {
    throw SecurityException("Manifest not signed by ThemisDB Team!");
}
```

### ✅ Integrität

**Frage:** Wurde die Binary nach dem Build manipuliert?

**Antwort:** Nein, durch Hash-Verifikation!

```cpp
// SHA-256 Hash im Manifest ist Teil der Signatur
// Wenn Binary manipuliert wird, stimmt Hash nicht mehr

std::string calculated_hash = SHA256(binary_file);
std::string expected_hash = manifest.files[0].sha256;

if (calculated_hash != expected_hash) {
    throw SecurityException("Binary has been tampered with!");
}
```

### ✅ Vollständigkeit

**Frage:** Sind alle notwendigen Dateien vorhanden?

**Antwort:** Ja, durch Manifest-Liste!

```json
{
  "files": [
    {"path": "bin/themis_server", "sha256": "abc123..."},
    {"path": "lib/themis_core.so", "sha256": "def456..."},
    {"path": "lib/themis_utils.so", "sha256": "ghi789..."}
  ]
}
```

Alle Dateien müssen heruntergeladen UND verifiziert werden.

## Praktisches Beispiel

### Manifest-Struktur

```json
{
  "version": "1.2.0",
  "tag_name": "v1.2.0",
  "release_date": "2025-01-20T10:00:00Z",
  "is_critical": true,
  
  "files": [
    {
      "path": "bin/themis_server",
      "type": "executable",
      "platform": "linux",
      "architecture": "x64",
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "size_bytes": 2048576,
      "download_url": "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/themis_server"
    },
    {
      "path": "lib/themis_core.so",
      "type": "library",
      "platform": "linux",
      "architecture": "x64",
      "sha256": "a7b8c9d0e1f2g3h4i5j6k7l8m9n0o1p2q3r4s5t6u7v8w9x0y1z2",
      "size_bytes": 5242880,
      "download_url": "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/themis_core.so"
    }
  ],
  
  "build_info": {
    "commit_sha": "abc123def456",
    "build_date": "2025-01-20T09:00:00Z",
    "compiler": "gcc 11.4.0",
    "build_flags": "-O3 -march=x86-64"
  },
  
  "signature": {
    "algorithm": "RSA-SHA256",
    "signature": "MEUCIQDxyz...",  // Base64-encoded signature
    "certificate": "-----BEGIN CERTIFICATE-----\nMIIBIjAN...",
    "timestamp": "2025-01-20T10:00:00Z",
    "timestamp_authority": "http://timestamp.themisdb.io"
  }
}
```

### Verifikations-Workflow

```cpp
class ManifestVerifier {
public:
    bool verifyRelease(const std::string& version) {
        // 1. Download Manifest
        std::string manifest_url = 
            "https://github.com/makr-code/ThemisDB/releases/download/"
            + version + "/manifest.json";
        
        std::string manifest_json = httpGet(manifest_url);
        auto manifest = ReleaseManifest::fromJson(json::parse(manifest_json));
        
        // 2. Verify Manifest Signature
        LOG_INFO("Verifying manifest signature...");
        if (!verifyManifestSignature(manifest)) {
            LOG_ERROR("❌ MANIFEST SIGNATURE INVALID!");
            LOG_ERROR("This manifest was NOT signed by ThemisDB Team!");
            LOG_ERROR("REFUSING to install - potential security breach!");
            return false;
        }
        LOG_INFO("✅ Manifest signature valid - from ThemisDB Team");
        
        // 3. Download and Verify Each File
        for (const auto& file : manifest.files) {
            LOG_INFO("Downloading {}...", file.path);
            
            // Download
            std::string file_data = httpGet(file.download_url);
            
            // Verify Size
            if (file_data.size() != file.size_bytes) {
                LOG_ERROR("❌ SIZE MISMATCH for {}", file.path);
                LOG_ERROR("Expected: {} bytes, Got: {} bytes", 
                    file.size_bytes, file_data.size());
                return false;
            }
            
            // Verify Hash
            std::string calculated_hash = SHA256(file_data);
            if (calculated_hash != file.sha256) {
                LOG_ERROR("❌ HASH MISMATCH for {}", file.path);
                LOG_ERROR("Expected: {}", file.sha256);
                LOG_ERROR("Got:      {}", calculated_hash);
                LOG_ERROR("File has been TAMPERED WITH!");
                return false;
            }
            
            LOG_INFO("✅ {} verified - authentic and intact", file.path);
            
            // Save file
            saveFile(file.path, file_data);
        }
        
        LOG_INFO("✅ ALL FILES VERIFIED - Release is authentic!");
        return true;
    }
    
private:
    bool verifyManifestSignature(const ReleaseManifest& manifest) {
        // 1. Extract signature components
        auto signature = base64Decode(manifest.signature.signature);
        auto certificate = parseCertificate(manifest.signature.certificate);
        
        // 2. Verify certificate chain
        if (!verifyCertificateChain(certificate, trusted_root_ca)) {
            LOG_ERROR("Certificate chain invalid");
            return false;
        }
        
        // 3. Check certificate revocation
        if (isCertificateRevoked(certificate)) {
            LOG_ERROR("Certificate has been REVOKED!");
            return false;
        }
        
        // 4. Verify signature
        // Remove signature field from manifest for verification
        auto manifest_copy = manifest;
        manifest_copy.signature.signature = "";
        std::string manifest_data = manifest_copy.toJson().dump();
        
        bool valid = verifyRSASignature(
            manifest_data,
            signature,
            certificate.publicKey
        );
        
        return valid;
    }
};
```

## Attack Scenarios - Wie das Manifest schützt

### Scenario 1: Kompromittiertes GitHub

```
Angreifer:
- Hackt GitHub Account
- Uploaded manipulierte Binary "themis_server"
- ABER: Kann Manifest nicht neu signieren (hat privaten Schlüssel nicht)

User:
1. Download manifest.json
2. Download themis_server (manipuliert!)
3. Verify manifest signature ✅ (Original-Manifest noch da)
4. Calculate hash of themis_server
5. Compare with manifest hash
6. ❌ HASH MISMATCH - Installation abgebrochen!

Ergebnis: ✅ Angriff blockiert
```

### Scenario 2: Man-in-the-Middle

```
Angreifer:
- Abfangen des Downloads
- Ersetzt themis_server mit Malware
- ABER: Kann Manifest nicht ändern (Signatur wird ungültig)

User:
1. Download manifest.json (abgefangen, aber Signatur bleibt)
2. Download themis_server (ersetzt mit Malware!)
3. Verify hash
4. ❌ HASH MISMATCH

Ergebnis: ✅ Angriff blockiert
```

### Scenario 3: Gefälschtes Manifest

```
Angreifer:
- Erstellt eigenes Manifest mit Malware-Hashes
- Versucht zu signieren
- ABER: Hat privaten Schlüssel nicht

User:
1. Download gefälschtes manifest.json
2. Verify signature
3. ❌ SIGNATURE INVALID (nicht von ThemisDB Team)

Ergebnis: ✅ Angriff blockiert
```

## Technische Details

### Signatur-Algorithmus

**RSA-SHA256 oder Ed25519:**

```cpp
// Generate Key Pair (einmalig beim Setup)
openssl genrsa -out themisdb_release_private.pem 4096
openssl req -new -x509 -key themisdb_release_private.pem \
    -out themisdb_release_cert.pem -days 3650

// Sign Manifest (bei jedem Release)
openssl dgst -sha256 -sign themisdb_release_private.pem \
    -out manifest.sig manifest.json

// Verify Signature (bei Installation)
openssl dgst -sha256 -verify public_key.pem \
    -signature manifest.sig manifest.json
```

### Hash-Algorithmus

**SHA-256 (256-bit Kryptographischer Hash):**

```cpp
std::string calculateSHA256(const std::string& data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data.c_str(), data.size());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') 
           << (int)hash[i];
    }
    return ss.str();
}
```

## Chain of Trust

```
Root CA (Vertrauenswürdig)
    │
    ├── Intermediate CA
    │       │
    │       └── ThemisDB Release Certificate
    │               │
    │               └── Manifest Signature
    │                       │
    │                       └── Binary Hashes
    │                               │
    │                               └── Actual Binaries
```

Jede Ebene verifiziert die nächste!

## Zusammenfassung

### Das Manifest-Prinzip garantiert:

✅ **Authentizität**: Binary kommt wirklich vom ThemisDB Team
✅ **Integrität**: Binary wurde nicht manipuliert
✅ **Vollständigkeit**: Alle Dateien sind vorhanden
✅ **Aktualität**: Timestamp beweist, wann signiert wurde

### Das Manifest-Prinzip schützt gegen:

✅ Kompromittierte GitHub Accounts
✅ Man-in-the-Middle Attacken
✅ Manipulierte Downloads
✅ CDN-Kompromittierung
✅ Gefälschte Releases

### Das Manifest-Prinzip ist:

✅ **Industry Standard** (verwendet von Kubernetes, Docker, APT, etc.)
✅ **Kryptographisch sicher** (RSA-4096 oder Ed25519)
✅ **Transparent** (Jeder kann Signatur prüfen)
✅ **Einfach zu verifizieren** (Standard OpenSSL Tools)

## Fazit

**Das signierte Manifest ist der Kern der Release-Sicherheit:**

Ohne Manifest: "Hoffe, dass GitHub nicht gehackt wird"
Mit Manifest: "Kryptographischer Beweis der Authentizität"

**Genau dafür ist das Manifest-Prinzip da! 🔒**
