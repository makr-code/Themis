# Governance Policy Engine - Usage Examples

This document demonstrates how to use the YAML-based governance policy engine in Themis.

## Overview

The Governance Policy Engine provides:
- **Classification-based data protection** (offen, vs-nfd, geheim, streng-geheim)
- **Fine-grained access control** (ANN, export, cache)
- **Retention policies** per classification
- **Encrypt-then-Sign** log handling
- **Observe/Enforce modes** for gradual rollout

## Configuration

Governance policies are defined in `config/governance.yaml`:

```yaml
vs_classification:
  offen:
    encryption_required: false
    ann_allowed: true
    export_allowed: true
    cache_allowed: true
    redaction_level: "none"
    retention_days: 365
    log_encryption: false
  
  geheim:
    encryption_required: true
    ann_allowed: false  # ANN disabled; exact search only
    export_allowed: false
    cache_allowed: false
    redaction_level: "strict"
    retention_days: 3650  # 10 years
    log_encryption: true

enforcement:
  resource_mapping:
    "/admin/*": "vs-nfd"
    "/admin/status": "vs-nfd"  # explizit gemappt
    "/vector/search": "offen"
  
  default_mode: "enforce"
```

### Konfig-Dateipfade (Suchreihenfolge)

Der Server lädt `governance.yaml` aus den folgenden Pfaden (erste gefundene Datei gewinnt):

1. `config/governance.yaml` (aus Repo-Root gestartet)
2. `../config/governance.yaml` (selten, falls CWD `build/` ist)
3. `../../config/governance.yaml` (CTest/IDE: CWD ist häufig `build/<Config>` wie `build/Release`)
4. `./governance.yaml` (Fallback im aktuellen Verzeichnis)

Hinweis: Diese Reihenfolge stellt sicher, dass CTest-Läufe aus `build/<Config>` die zentrale Konfiguration unter `config/` finden.

## HTTP Headers

### Request Headers

Clients can specify governance requirements via HTTP headers:

- `X-Classification`: Data classification level (offen, vs-nfd, geheim, streng-geheim)
- `X-Governance-Mode`: Enforcement mode (enforce, observe)
- `X-Encrypt-Logs`: Force log encryption (true, false, auto)
- `X-Redaction-Level`: Redaction profile (none, standard, strict)

### Response Headers

Server returns applied policy decisions:

- `X-Themis-Policy`: Compact policy summary
- `X-Themis-Integrity`: Signature status (signed-ciphertext:policy-only)
- `X-Themis-ANN`: ANN availability (allowed, disabled)
- `X-Themis-Content-Enc`: Content encryption requirement (required, optional)
- `X-Themis-Export`: Export permission (allowed, forbidden)
- `X-Themis-Cache`: Cache permission (allowed, disabled)
- `X-Themis-Retention-Days`: Data retention period

Im Observe-Modus können zusätzlich Warnhinweise erscheinen:

- `X-Themis-Policy-Warn`: z. B. `ann_disabled_but_observed` oder `content_encryption_required_but_observed`

## Usage Examples

### Example 1: Public Data (offen)

**Request:**
```http
POST /vector/search
X-Classification: offen
Content-Type: application/json

{
  "vector": [0.1, 0.2, ...],
  "k": 10
}
```

**Response:**
```http
HTTP/1.1 200 OK
X-Themis-Policy: classification=offen;mode=enforce;encrypt_logs=false;redaction=none
X-Themis-ANN: allowed
X-Themis-Content-Enc: optional
X-Themis-Export: allowed
X-Themis-Cache: allowed
X-Themis-Retention-Days: 365

{
  "results": [...]
}
```

### Example 2: Secret Data (geheim) - Enforce Mode

**Request:**
```http
POST /vector/search
X-Classification: geheim
X-Governance-Mode: enforce
Content-Type: application/json

{
  "vector": [0.1, 0.2, ...],
  "k": 10
}
```

**Response:**
```http
HTTP/1.1 403 Forbidden
X-Themis-Policy: classification=geheim;mode=enforce;encrypt_logs=true;redaction=strict
X-Themis-ANN: disabled

{
  "error": true,
  "message": "Approximate vector search (ANN) is disabled for classification 'geheim'"
}
```

### Example 3: Secret Data (geheim) - Observe Mode

**Request:**
```http
POST /vector/search
X-Classification: geheim
X-Governance-Mode: observe
Content-Type: application/json

{
  "vector": [0.1, 0.2, ...],
  "k": 10
}
```

**Response:**
```http
HTTP/1.1 200 OK
X-Themis-Policy: classification=geheim;mode=observe;encrypt_logs=true;redaction=strict
X-Themis-ANN: disabled
X-Themis-Policy-Warn: ann_disabled_but_observed
X-Themis-Export: forbidden
X-Themis-Cache: disabled
X-Themis-Retention-Days: 3650

{
  "results": [...]
}
```

### Example 4: Content Import with Encryption Requirement

**Request:**
```http
POST /content/import
X-Classification: streng-geheim
X-Governance-Mode: enforce
Content-Type: application/json

{
  "content": {
    "id": "doc123",
    "mime_type": "application/pdf",
    "encrypted": false
  },
  "blob": "base64encodeddata..."
}
```

**Response:**
```http
HTTP/1.1 422 Unprocessable Entity
X-Themis-Policy: classification=streng-geheim;mode=enforce;encrypt_logs=true;redaction=strict

{
  "error": true,
  "message": "Content encryption required for classification 'streng-geheim'"
}
```

### Example 5: Default Classification from Resource Mapping

**Request:**
```http
GET /admin/backup
```

**Response:**
```http
HTTP/1.1 200 OK
X-Themis-Policy: classification=vs-nfd;mode=enforce;encrypt_logs=true;redaction=standard
X-Themis-Retention-Days: 1825

{
  "status": "ok"
}
```

## Classification Profiles

### offen (Public)
- No encryption required
- ANN allowed
- Export/cache allowed
- 1 year retention

### vs-nfd (Restricted)
- Encryption required
- ANN allowed
- Export/cache allowed
- 5 years retention

### geheim (Secret)
- Encryption required
- **ANN disabled** (exact search only)
- Export/cache forbidden
- 10 years retention

### streng-geheim (Top Secret)
- Encryption required
- **ANN disabled**
- Export/cache forbidden
- 20 years retention

## Gradual Rollout with Observe Mode

Use `X-Governance-Mode: observe` to test policies without enforcement:

1. Deploy with `default_mode: observe` in `governance.yaml`
2. Monitor `X-Themis-Policy-Warn` headers in production
3. Identify and fix policy violations
4. Switch to `default_mode: enforce`

## Policy Customization

Edit `config/governance.yaml` to customize:
- Classification levels and their properties
- Resource-to-classification mappings
- Default enforcement mode
- Retention periods
- SAGA signing and log encryption settings

Changes take effect after server restart (hot-reload planned for future).

## Integration with Encryption

The governance engine works with the encryption strategy:
- `encryption_required: true` → Data-at-rest encryption mandatory
- `log_encryption: true` → SAGA/Audit logs encrypted before PKI signing (Encrypt-then-Sign)
- Classification determines key hierarchy and access controls

## Compliance Frameworks

Supported frameworks (configurable in `governance.yaml`):
- GDPR (EU General Data Protection Regulation)
- VSA (German Federal Classification System)
- BSI-C5 (German Cloud Security Standard)

## API Reference

### GET /ts/config
Returns current time-series compression configuration.

### PUT /ts/config
Updates time-series compression settings.

### All Endpoints
All endpoints respect governance headers and return policy decisions in response headers.

## Testing

Test policy enforcement with curl:

```bash
# Test ANN restriction (enforce)
curl -X POST http://localhost:8080/vector/search \
  -H "X-Classification: geheim" \
  -H "X-Governance-Mode: enforce" \
  -H "Content-Type: application/json" \
  -d '{"vector": [0.1, 0.2], "k": 10}'
# Expected: 403 Forbidden

# Test ANN restriction (observe)
curl -X POST http://localhost:8080/vector/search \
  -H "X-Classification: geheim" \
  -H "X-Governance-Mode: observe" \
  -H "Content-Type: application/json" \
  -d '{"vector": [0.1, 0.2], "k": 10}'
# Expected: 200 OK with X-Themis-Policy-Warn header
```

### Gezielte Testläufe (CTest)

Unter Windows PowerShell lassen sich gezielt nur Governance- oder Time-Series-Tests ausführen. Beispiele (aus `build/`):

```powershell
# Alle Governance-Tests (mehrere Suites via Regex)
ctest -C Release -R "StatsApiTest|MetricsApiTest|HttpRangeIndexTest|HttpGovernanceTest" --output-on-failure

# Nur drei spezifische Governance-Fälle
ctest -C Release -R "HttpGovernanceTest.Classification_VsNfd_RequiresEncryption|HttpGovernanceTest.ResourceMapping_AppliesClassification|HttpGovernanceTest.RetentionDays_ReturnsPolicy" --output-on-failure

# Alle Time-Series-bezogenen Suites
ctest -C Release -R "^HttpTimeSeriesTest\.|^TSStoreTest\.|^GorillaCodecTest\.|^ContinuousAggTest\." --output-on-failure
```

Tipps:
- Verwende Anker (`^`) und das Escapen des Punkts (`\.`) für exakte Präfix-Matches.
- In PowerShell sind doppelte Anführungszeichen empfohlen, damit Regex-Sonderzeichen korrekt übergeben werden.

## Future Enhancements

Planned features:
- Hot-reload of `governance.yaml` without restart
- Per-user classification overrides (via JWT claims)
- Audit trail for policy violations
- Automated compliance reports
- Field-level encryption based on classification
