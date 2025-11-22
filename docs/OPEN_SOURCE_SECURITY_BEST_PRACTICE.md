# ThemisDB Release Security - Best Practice für Open Source

## Klarstellung

**ThemisDB ist ein Open-Source-Projekt ohne Kundenstamm.**

Daher:
- ❌ **KEINE** Manifest-Verschlüsselung
- ✅ **Fokus auf** Signatur-basierte Integrität und Authentizität
- ✅ **Transparenz** über Security through Obscurity

## Empfohlene Sicherheitsarchitektur

### 1. Signierte Manifests (Authentizität)

```json
// manifest.json - Öffentlich auf GitHub
{
  "version": "1.2.0",
  "tag_name": "v1.2.0",
  "release_date": "2025-01-20T10:00:00Z",
  "is_critical": true,
  "release_notes": "Security fixes...",
  
  "files": [
    {
      "path": "bin/themis_server",
      "sha256": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      "size_bytes": 1024000,
      "download_url": "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/themis_server"
    }
  ],
  
  "signature": {
    "algorithm": "CMS-SHA256",
    "signature": "...",
    "certificate": "...",
    "timestamp": "2025-01-20T10:00:00Z"
  }
}
```

### 2. SHA-256 Hashes (Integrität)

Jede Datei im Manifest hat:
- SHA-256 Hash
- Größe in Bytes
- Download-URL

### 3. HTTPS Transport (Vertraulichkeit während Übertragung)

- TLS 1.3 für alle Downloads
- GitHub's CDN-Infrastruktur

## Workflow

### Build-Zeit (Release erstellen)

```bash
# 1. Manifest generieren
./tools/generate_manifest.sh \
  --version 1.2.0 \
  --release-dir ./build/release \
  --output manifest.json

# 2. Signieren
openssl cms -sign \
  -in manifest.json \
  -signer release-cert.pem \
  -inkey release-key.pem \
  -binary \
  -outform DER \
  -out manifest.json.sig

# 3. Upload zu GitHub
gh release upload v1.2.0 \
  manifest.json \
  themis_server \
  themis_core.so
```

### Runtime (Update-Check)

```cpp
// 1. Download Manifest
std::string manifest_json = downloadFromGitHub(
    "https://github.com/makr-code/ThemisDB/releases/download/v1.2.0/manifest.json"
);

// 2. Parse Manifest
auto manifest = ReleaseManifest::fromJson(json::parse(manifest_json));

// 3. Verify Signature
if (!verifyManifestSignature(manifest)) {
    throw SecurityException("Invalid signature");
}

// 4. Download & Verify Files
for (const auto& file : manifest.files) {
    downloadFile(file.download_url);
    verifyFileHash(file.path, file.sha256);
}

// 5. Apply Update
applyUpdate(manifest);
```

## Vorteile für Open Source

### ✅ Transparenz
- Jeder kann Manifests einsehen
- Community kann Security-Reviews machen
- Keine versteckten Komponenten

### ✅ Einfachheit
- Kein Key Management
- Keine Verschlüsselung
- Standard-Tools funktionieren

### ✅ Vertrauen
- Öffentliche Signatur-Verifikation
- Reproduzierbare Builds
- Supply Chain Security

### ✅ Compliance
- Entspricht Open-Source Best Practices
- Wie Kubernetes, Docker, Debian, etc.
- Industry Standard

## Sicherheitsgarantien

### Was IST garantiert:
✅ Manifest wurde von ThemisDB Team signiert
✅ Manifest wurde nicht manipuliert
✅ Dateien haben korrekte Hashes
✅ Keine Man-in-the-Middle Attacken (HTTPS)

### Was NICHT garantiert ist:
❌ Vertraulichkeit der Manifest-Inhalte (nicht nötig, ist öffentlich)
❌ Anonymität der Downloads (GitHub Analytics)

## Fazit

**Für ein Open-Source-Projekt wie ThemisDB:**
- Signierte Manifests = ✅ Ausreichend und Best Practice
- Verschlüsselte Manifests = ❌ Unnötig und kontraproduktiv

**Die bereits implementierte Lösung mit UpdateChecker + Signaturen ist perfekt!**
