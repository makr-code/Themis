# Verschlüsselte Release Manifests - Sicherheitskonzept

## Problem

Wie stellen wir sicher, dass:
1. Release-Manifests verschlüsselt auf GitHub liegen
2. Jede ThemisDB-Instanz einen eigenen Entschlüsselungsschlüssel in ihrer Datenbank hat
3. Nur autorisierte Instanzen Manifests entschlüsseln können

## Architektur-Übersicht

```
┌──────────────────────────────────────────────────────────┐
│                  GitHub Release (Public)                  │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌────────────────────────────────────────────────┐     │
│  │  encrypted_manifest.json                       │     │
│  │  - Verschlüsselt mit AES-256-GCM               │     │
│  │  - Signiert mit CMS/PKCS#7                     │     │
│  │  - Öffentlich lesbar, aber nicht entschlüsselbar│    │
│  └────────────────────────────────────────────────┘     │
│                                                          │
└──────────────────────────────────────────────────────────┘
                         │
                         │ HTTPS Download
                         ▼
┌──────────────────────────────────────────────────────────┐
│            ThemisDB Instanz (z.B. Kunde A)               │
├──────────────────────────────────────────────────────────┤
│                                                          │
│  ┌────────────────────────────────────────────────┐     │
│  │  RocksDB (Lokal)                               │     │
│  │  ┌──────────────────────────────────────┐      │     │
│  │  │ KEK (Key Encryption Key)             │      │     │
│  │  │ - Master-Schlüssel der Instanz       │      │     │
│  │  │ - Verschlüsselt MDK                  │      │     │
│  │  └──────────────────────────────────────┘      │     │
│  │  ┌──────────────────────────────────────┐      │     │
│  │  │ MDK (Manifest Decryption Key)        │      │     │
│  │  │ - Verschlüsselt mit KEK gespeichert  │      │     │
│  │  │ - Entschlüsselt Release-Manifests    │      │     │
│  │  └──────────────────────────────────────┘      │     │
│  └────────────────────────────────────────────────┘     │
│                                                          │
│  ┌────────────────────────────────────────────────┐     │
│  │  ManifestEncryption                            │     │
│  │  1. Download encrypted_manifest.json           │     │
│  │  2. Verify CMS signature (public)              │     │
│  │  3. Decrypt with local MDK                     │     │
│  │  4. Parse and validate manifest                │     │
│  └────────────────────────────────────────────────┘     │
│                                                          │
└──────────────────────────────────────────────────────────┘
```

## Schlüsselhierarchie

### 1. KEK (Key Encryption Key)
- **Zweck**: Master-Schlüssel für die ThemisDB-Instanz
- **Speicherort**: RocksDB, verschlüsselt mit PKI oder HSM
- **Lebensdauer**: Permanent, außer bei Rotation
- **Verwendung**: Verschlüsselt alle DEKs (Data Encryption Keys)

### 2. MDK (Manifest Decryption Key)
- **Zweck**: Entschlüsselung von Release-Manifests
- **Speicherort**: RocksDB, verschlüsselt mit KEK
- **Lebensdauer**: Permanent pro Instanz
- **Verwendung**: Nur für Manifest-Entschlüsselung

### 3. Manifest Encryption Key (MEK)
- **Zweck**: Verschlüsselung von Release-Manifests (Build-Zeit)
- **Speicherort**: Sicher beim Release-Team/CI
- **Lebensdauer**: Pro Release oder rotiert
- **Verwendung**: Verschlüsselt Manifests vor GitHub-Upload

## Workflow

### A. Release-Prozess (Build-Zeit)

```bash
# 1. Build Release
make release VERSION=1.2.0

# 2. Generate Manifest
./tools/generate_manifest.sh \
  --version 1.2.0 \
  --release-dir ./build/release

# Output: manifest.json (plaintext)

# 3. Encrypt Manifest
./tools/encrypt_manifest.sh \
  --input manifest.json \
  --output encrypted_manifest.json \
  --key-id manifest_encryption_key_v1

# Output: encrypted_manifest.json
{
  "schema_version": 1,
  "encryption_algorithm": "AES-256-GCM",
  "key_id": "manifest_decryption_key",
  "key_version": 1,
  "encrypted_blob": {
    "key_id": "manifest_decryption_key",
    "key_version": 1,
    "iv": "YWJjZGVmZ2hpams=",
    "ciphertext": "...",
    "tag": "..."
  },
  "version": "1.2.0",      // Public metadata
  "tag_name": "v1.2.0",
  "is_critical": true
}

# 4. Sign Encrypted Manifest (CMS/PKCS#7)
openssl cms -sign \
  -in encrypted_manifest.json \
  -out encrypted_manifest.json.sig \
  -signer release_cert.pem \
  -inkey release_key.pem

# 5. Upload to GitHub Release
gh release upload v1.2.0 \
  encrypted_manifest.json \
  encrypted_manifest.json.sig
```

### B. Installation (Erste Einrichtung)

```bash
# Beim ersten Start einer ThemisDB-Instanz
themis_server --init

# Intern:
1. KEK initialisieren (PKIKeyProvider)
2. MDK generieren und mit KEK verschlüsseln
3. MDK in RocksDB speichern
```

```cpp
// Pseudocode
void initializeInstance() {
    // 1. Initialize KeyProvider with KEK
    auto key_provider = std::make_shared<PKIKeyProvider>(
        pki_client, storage, "themisdb_instance_1"
    );
    
    // 2. Initialize FieldEncryption
    auto field_encryption = std::make_shared<FieldEncryption>(key_provider);
    
    // 3. Initialize ManifestEncryption
    auto manifest_encryption = std::make_shared<ManifestEncryption>(
        field_encryption, key_provider
    );
    
    // 4. Generate and store MDK
    if (!manifest_encryption->hasManifestKey()) {
        uint32_t version = manifest_encryption->initializeManifestKey();
        LOG_INFO("Initialized manifest key v{}", version);
    }
}
```

### C. Update-Prozess (Runtime)

```cpp
// 1. Download encrypted manifest from GitHub
std::string encrypted_data = downloadFromGitHub(
    "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/encrypted_manifest.json"
);

// 2. Verify signature (public operation, before decryption)
std::string signature = downloadFromGitHub(
    "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/encrypted_manifest.json.sig"
);

bool verified = manifest_encryption->verifyEncryptedManifestSignature(
    encrypted_data, signature, release_certificate
);

if (!verified) {
    throw SecurityException("Manifest signature verification failed");
}

// 3. Decrypt manifest with local MDK
ReleaseManifest manifest = manifest_encryption->decryptManifest(encrypted_data);

// 4. Validate manifest content
if (!validateManifest(manifest)) {
    throw ValidationException("Invalid manifest content");
}

// 5. Proceed with update...
```

## Sicherheitsmerkmale

### 1. Vertraulichkeit

**Problem**: Manifests enthalten sensible Informationen
- Dateinamen und Pfade
- Build-Commits und interne Metadaten
- Potentiell Sicherheitsdetails

**Lösung**: AES-256-GCM Verschlüsselung
- Nur autorisierte Instanzen können entschlüsseln
- Öffentlich auf GitHub, aber geschützt

### 2. Integrität

**Problem**: Manipulierte Manifests könnten schadhafte Updates liefern

**Lösung**: Mehrschichtige Validierung
1. **CMS-Signatur** auf verschlüsseltem Manifest (öffentlich prüfbar)
2. **GCM Authentication Tag** (Integritätsschutz)
3. **Manifest-Hash** nach Entschlüsselung
4. **Datei-Hashes** (SHA-256) für jede Datei

### 3. Authentizität

**Problem**: Nur legitime Releases dürfen installiert werden

**Lösung**: Digitale Signaturen
- Release-Team signiert mit PKI-Zertifikat
- Signatur wird vor Entschlüsselung geprüft
- Certificate Chain Verification bis zum Root CA

### 4. Schlüsselisolation

**Problem**: Kompromittierung einer Instanz

**Lösung**: Instanz-spezifische Schlüssel
- Jede ThemisDB-Instanz hat eigenen MDK
- KEK schützt MDK in Datenbank
- HSM-Integration für höchste Sicherheit

## Schlüsselmanagement

### Initial Setup

```bash
# Instanz A
themis_server --init
# Generiert: MDK_A (gespeichert in DB_A)

# Instanz B
themis_server --init
# Generiert: MDK_B (gespeichert in DB_B)
```

**Wichtig**: MDK_A ≠ MDK_B
- Jede Instanz hat eigenen Schlüssel
- Kompromittierung von A gefährdet nicht B

### Key Distribution

**Wie erhalten Instanzen den gleichen MEK?**

**Option 1: Symmetric Release Encryption** (Aktuell)
- Ein MEK für alle Releases
- Jede Instanz bekommt MEK bei Installation
- Problem: Alle Instanzen teilen gleichen Schlüssel

**Option 2: Asymmetric Per-Instance Encryption** (Besser)
- Jede Instanz hat RSA-Schlüsselpaar
- Public Key registriert bei Release-Server
- Manifest verschlüsselt mit Public Keys aller Instanzen
- Problem: Skaliert nicht bei vielen Instanzen

**Option 3: Hybrid Encryption** (Beste Lösung)
```
1. Release-Manifest mit symmetrischem Key verschlüsseln (MEK)
2. MEK mit Public Key jeder Instanz verschlüsseln
3. Alle verschlüsselten MEKs in Manifest einbetten

encrypted_manifest.json:
{
  "encrypted_keys": [
    {"instance_id": "A", "encrypted_mek": "..."},
    {"instance_id": "B", "encrypted_mek": "..."}
  ],
  "encrypted_data": "..."  // Manifest encrypted with MEK
}

Jede Instanz:
1. Findet ihren encrypted_mek
2. Entschlüsselt MEK mit privatem Schlüssel
3. Entschlüsselt Manifest mit MEK
```

### Key Rotation

```cpp
// Rotate MDK (z.B. nach Sicherheitsvorfall)
uint32_t new_version = manifest_encryption->initializeManifestKey();
LOG_INFO("Rotated to manifest key v{}", new_version);

// Alte Version bleibt für Entschlüsselung alter Manifests
// Neue Version für zukünftige Verschlüsselung
```

### Key Backup

```cpp
// Export für Disaster Recovery
std::string encrypted_bundle = manifest_encryption->exportManifestKey(
    "secure_backup_password_123"
);

// Speichern an sicherem Ort (z.B. Vault, Offline-Storage)
saveToSecureLocation(encrypted_bundle);

// Restore nach Datenverlust
manifest_encryption->importManifestKey(
    encrypted_bundle,
    "secure_backup_password_123"
);
```

## Integration in Updates-Workflow

### UpdateChecker Integration

```cpp
// In UpdateChecker::checkForUpdates()
auto latest_release = fetchLatestRelease();

// Download encrypted manifest
std::string encrypted_manifest_url = 
    latest_release.download_url + "/encrypted_manifest.json";
std::string encrypted_data = httpGet(encrypted_manifest_url);

// Decrypt and parse
try {
    ReleaseManifest manifest = manifest_encryption_->decryptManifest(encrypted_data);
    
    // Store in ManifestDatabase
    manifest_db_->storeManifest(manifest);
    
    // Continue with update check...
} catch (const DecryptionException& e) {
    LOG_ERROR("Failed to decrypt manifest: {}", e.what());
    // Fall back to public manifest if available
}
```

### ManifestDatabase Integration

```cpp
// In ManifestDatabase::storeManifest()
bool ManifestDatabase::storeManifest(const ReleaseManifest& manifest) {
    // Manifests werden entschlüsselt gespeichert
    // (bereits in der DB verschlüsselt durch RocksDB encryption-at-rest)
    
    std::string key = manifest.version;
    std::string value = manifest.toJson().dump();
    
    storage_->Put(cf_manifests_, key, value);
    
    return true;
}
```

## Deployment-Szenarios

### Szenario 1: On-Premise Installation

```
Kunde A: ThemisDB in eigenem Rechenzentrum
- Eigener MDK in lokaler DB
- KEK gesichert mit HSM
- Updates automatisch entschlüsselt
```

### Szenario 2: Cloud-Deployment

```
Kunde B: ThemisDB in AWS
- MDK in RDS (encrypted-at-rest)
- KEK in AWS KMS
- Updates via S3 Gateway
```

### Szenario 3: Multi-Tenant SaaS

```
SaaS-Provider: Viele Kunden auf einer Plattform
- Ein MDK pro Tenant-Datenbank
- Zentrale Manifest-Verteilung
- Audit-Trail pro Tenant
```

## Vorteile

1. **End-to-End Vertraulichkeit**
   - Manifest verschlüsselt auf GitHub
   - Verschlüsselt während Download
   - Entschlüsselt nur in autorisierter Instanz

2. **Defense in Depth**
   - Signatur-Verifikation (Authentizität)
   - Verschlüsselung (Vertraulichkeit)
   - Hash-Checks (Integrität)

3. **Compliance-Ready**
   - DSGVO: Verschlüsselung von Metadaten
   - SOC 2: Key Management und Rotation
   - ISO 27001: Kryptographische Controls

4. **Flexible Deployment**
   - On-Premise mit eigenem KEK
   - Cloud mit KMS-Integration
   - HSM für höchste Sicherheit

## Nächste Schritte

1. ✅ `ManifestEncryption` Klasse implementiert
2. ✅ `EncryptedManifest` Datenstruktur definiert
3. ⏳ Integration in `UpdateChecker`
4. ⏳ Integration in `ManifestDatabase`
5. ⏳ Build-Tool für Manifest-Verschlüsselung
6. ⏳ GitHub Actions Workflow
7. ⏳ Dokumentation und Tests

## Offene Fragen

1. **Key Distribution**: Wie erhalten neue Instanzen den MDK?
   - Antwort: Generiert bei Installation, nicht verteilt

2. **Key Escrow**: Backup-Strategie für MDK?
   - Antwort: Export mit Passwort-Verschlüsselung

3. **Revocation**: Wie sperren wir kompromittierte Instanzen?
   - Antwort: Certificate Revocation List (CRL) + neue Releases nur mit neuen Keys

4. **Performance**: Overhead durch Verschlüsselung?
   - Antwort: Minimal (~1ms für Manifest, einmalig beim Download)
