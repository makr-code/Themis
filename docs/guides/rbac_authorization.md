# RBAC & ABAC Authorization

Diese Seite beschreibt die vollständige Zugriffskontrolle in ThemisDB mit zweistufigem Sicherheitsmodell:
1. **Token-basierte Authentifizierung** mit Scopes (RBAC-Ebene)
2. **Policy Engine** mit Attribute-Based Access Control (ABAC, Ranger-kompatibel)

## Konzept

ThemisDB implementiert ein **zweistufiges Sicherheitsmodell**:

### Stufe 1: Token-basierte Authentifizierung (RBAC)
- **Token-basiert:** API-Clients senden `Authorization: Bearer <token>` Header
- **Scopes:** Jeder Token hat einen Satz von Scopes (z. B. `admin`, `config:write`, `cdc:read`, `metrics:read`)
- **Endpoint-Schutz:** Sensitive Endpunkte prüfen erforderliche Scopes
- **Schnelle Prüfung:** Scope-Check erfolgt zuerst, bevor Policy Engine evaluiert wird

### Stufe 2: Policy Engine (ABAC)
- **PolicyEngine:** Ranger-kompatible Attribute-Based Access Control
- **Policies:** YAML/JSON-Konfiguration mit subject/action/resource/conditions
- **Feingranular:** Kontrolle auf Resource-Pfad-Ebene mit Wildcard-Support
- **IP-basiert:** Optional IP-Präfix-Filtering (z.B. nur interne Netzwerke)
- **Effect:** Allow/Deny mit Deny-Overrides-Semantik
- **Audit-Logs:** Alle Autorisierungsentscheidungen werden geloggt
- **Metriken:** Prometheus-Zähler für Autorisierungsergebnisse

**Evaluierungsreihenfolge:**
1. Token-Scope-Check (schnell, in-memory)
2. Falls Scope vorhanden → Policy Engine Evaluation (wenn konfiguriert)
3. Beide müssen erfolgreich sein für Zugriff

## Scope-Matrix

| Scope | Berechtigungen |
|-------|---------------|
| `admin` | Voller Zugriff auf alle Endpoints (Superuser) |
| `config:read` | GET /config |
| `config:write` | POST /config (Hot-Reload) |
| `cdc:read` | GET /changefeed/*, CDC Stats |
| `cdc:admin` | POST /changefeed/retention (Konfiguration) |
| `metrics:read` | GET /metrics (Prometheus) |
| `data:read` | GET /entities/*, /query/*, /graph/*, /vector/* |
| `data:write` | PUT/DELETE /entities/*, POST /query/aql (schreibend) |
| `pii:reveal` | GET /pii/reveal/{uuid} – Entschlüsselung pseudonymisierter Werte |
| `pii:erase` | DELETE /pii/{uuid} – DSGVO Art. 17 (soft/hard) |

## Konfiguration

Token werden in `config/auth.json` definiert (oder via ENV-Variablen):

```json
{
  "tokens": [
    {
      "token": "admin-secret-token-abc123",
      "user_id": "admin",
      "scopes": ["admin", "config:write", "config:read", "cdc:read", "cdc:admin", "metrics:read", "data:read", "data:write"]
    },
    {
      "token": "readonly-token-def456",
      "user_id": "monitoring",
      "scopes": ["metrics:read", "cdc:read", "data:read"]
    }
  ]
}
```

Alternativ via Umgebungsvariablen (für Container):

```bash
THEMIS_AUTH_TOKENS='[{"token":"abc123","user_id":"admin","scopes":["admin"]}]'
```

## API-Nutzung

Clients senden Token im Authorization-Header:

```bash
# Erfolg (admin hat admin-Scope)
curl -H "Authorization: Bearer admin-secret-token-abc123" \
     http://localhost:8765/config

# Verweigert (readonly hat keinen config:write-Scope)
curl -X POST -H "Authorization: Bearer readonly-token-def456" \
     -H "Content-Type: application/json" \
     -d '{"logging":{"level":"debug"}}' \
     http://localhost:8765/config
# -> 403 Forbidden
```

## Geschützte Endpunkte

MVP-Version schützt folgende Endpunkte:

- `POST /config` → Scope: `config:write`
- `GET /config` → Scope: `config:read`
- `GET /changefeed/*` → Scope: `cdc:read`
- `POST /changefeed/retention` → Scope: `cdc:admin`
- `GET /metrics` → Scope: `metrics:read` (optional, für private Deployments)
- Admin-Endpoints (z. B. `/admin/*`, falls vorhanden) → Scope: `admin`
- `GET /policies/export/ranger` → Scope: `admin`
- `POST /policies/import/ranger` → Scope: `admin`
- `GET /pii/reveal/{uuid}` → Scope: `pii:reveal` (Admin-Token alternativ zulässig)
- `DELETE /pii/{uuid}` → Scope: `pii:erase` (Admin-Token alternativ zulässig)

Zusätzlich stehen Policy-Management-Endpunkte zur Verfügung (Ranger-kompatibel):

- `GET /policies/export/ranger` → Scope: `admin`
     - Exportiert die aktuell geladene Policy-Liste in ein vereinfachtes Ranger-JSON-Format
     - Nützlich für Audits und zur Übernahme in andere Systeme
- `POST /policies/import/ranger` → Scope: `admin`
     - Importiert Policies aus einem externen Ranger-Service und lädt sie in die interne PolicyEngine
     - Erfordert Environment-Variablen für den Ranger-Client (siehe unten)

Datenendpunkte (`/entities`, `/query`, `/graph`, `/vector`) sind zunächst offen; optionale Aktivierung via Feature-Flag `require_data_auth`.

## Metriken

Prometheus `/metrics` enthält:

**Token/Scope-basiert (RBAC):**
- `themis_authz_success_total` — Erfolgreiche Autorisierungen (Label: `user_id`, `scope`)
- `themis_authz_denied_total` — Verweigerte Zugriffe (Label: `user_id`, `scope`, `reason`)
- `themis_authz_invalid_token_total` — Ungültige/fehlende Token

**Policy Engine (ABAC):**
- `themis_policy_allow_total` — Policies erlaubten Zugriff
- `themis_policy_deny_total` — Policies verweigerten Zugriff  
- `themis_policy_eval_total` — Gesamtzahl Policy-Evaluierungen

## Audit-Logs

Bei verweigertem Zugriff wird ein WARN-Log geschrieben:

```
WARN: Authorization denied for user 'monitoring': Missing required scope: config:write
```

Für vollständige Audit-Trails können strukturierte Logs (JSON) aktiviert werden (`POST /config` → `logging.format = "json"`).

## Sicherheitshinweise

- **Token-Rotation:** Aktuell statische Token; Rotation via Neustart oder `/config` Reload (geplant: Key-Rotation-API)
- **TLS:** In Produktion IMMER hinter TLS-Proxy (nginx/Caddy); Token sonst plain-text übertragen
- **Secrets-Management:** Token nicht in Git committen; nutze Secrets-Manager (Vault, K8s Secrets)
- **Least Privilege:** Verteile minimale Scopes; Admin-Token nur für ops/debugging
- **Policy-Dateien:** `config/policies.yaml` wird beim Start automatisch geladen
- **IP-Filtering:** Nutze `allowed_ip_prefixes` in Policies für sensible Endpunkte
- **Deny-Overrides:** Explizite Deny-Policies überschreiben Allow-Policies
- **Defense in Depth:** Beide Ebenen (Scopes + Policies) müssen zustimmen für Zugriff

## ABAC Policy Engine

### Policy-Struktur

```yaml
- id: unique-policy-id
  name: Human-readable description
  subjects: ["user1", "admin", "*"]  # "*" = wildcard für alle
  actions: ["read", "write", "delete", "query", "metrics.read", "pii.reveal"]
  resources: ["/entities/users:*", "/metrics", "/pii/reveal/*"]
  effect: allow  # oder deny
  allowed_ip_prefixes: ["10.0.", "192.168."]  # optional
```

### Konfiguration

Policies werden aus `config/policies.yaml` oder `config/policies.json` geladen:

```yaml
- id: allow-metrics-readonly
  name: readonly darf /metrics
  subjects: ["readonly"]
  actions: ["metrics.read"]
  resources: ["/metrics"]
  effect: allow

- id: deny-hr-external
  name: HR-Daten nur intern
  subjects: ["*"]
  actions: ["read"]
  resources: ["/entities/hr:*"]
  effect: deny
  # Nur wenn IP NICHT in allowed_ip_prefixes → deny
  allowed_ip_prefixes: ["10.0.", "192.168.1."]
```

### Resource-Matching

- **Prefix-basiert:** `/entities/users:*` matched `/entities/users:123`
- **Exact:** `/metrics` matched nur `/metrics`
- **Wildcard:** `*` matched alles

### Actions

Standard-Actions (erweiterbar):
- `read`, `write`, `delete`, `query`, `admin`
- `metrics.read`, `config.read`, `config.write`
- `cdc.read`, `cdc.admin`
- `pii.reveal`, `pii.erase`
- `vector.search`, `vector.write`

### Evaluierungslogik

1. **Deny-Overrides:** Wenn eine Deny-Policy matched → Zugriff verweigert
2. **Allow-Policies:** Mindestens eine Allow-Policy muss matchen
3. **Default:** Wenn keine Policies konfiguriert sind → Allow (fail-open für Migration)
4. **IP-Check:** Wenn `allowed_ip_prefixes` gesetzt, muss Client-IP matchen

### Policy-Management via API

```bash
# Export (für Backup/Audit)
curl -H "Authorization: Bearer $ADMIN_TOKEN" \
     http://localhost:8765/policies/export/ranger > policies_backup.json

# Import (von externem Ranger-Service)
export THEMIS_RANGER_BASE_URL=https://ranger.example.com
export THEMIS_RANGER_SERVICE=themisdb
export THEMIS_RANGER_BEARER=ranger-token

curl -X POST -H "Authorization: Bearer $ADMIN_TOKEN" \
     http://localhost:8765/policies/import/ranger
```

## Beispiel-Workflows

### 1) Monitoring-Setup

Token mit `metrics:read` + `cdc:read`:

```bash
curl -H "Authorization: Bearer readonly-token-def456" \
     http://localhost:8765/metrics
```

### 2) Config Hot-Reload

Token mit `config:write`:

```bash
curl -X POST \
     -H "Authorization: Bearer admin-secret-token-abc123" \
     -H "Content-Type: application/json" \
     -d '{"logging":{"level":"info"},"request_timeout_sec":60}' \
     http://localhost:8765/config
```

### 3) CDC-Subscription

Token mit `cdc:read`:

```bash
curl -H "Authorization: Bearer readonly-token-def456" \
     "http://localhost:8765/changefeed?from_seq=0&limit=100"
```

### 4) Policy Export (Ranger)

```bash
curl -H "Authorization: Bearer $THEMIS_TOKEN_ADMIN" \
     http://localhost:8765/policies/export/ranger
```

### 5) Policy Import (Ranger)
### 6) PII-Reveal

```bash
# Admin-Token ODER Token mit Scope pii:reveal
curl -H "Authorization: Bearer %THEMIS_TOKEN_ADMIN%" \
     http://localhost:8765/pii/reveal/11111111-1111-1111-1111-111111111111
```

Antwort:

```json
{"uuid":"11111111-1111-1111-1111-111111111111","value":"alice@example.com"}
```


Setze Ranger-Umgebung (z. B. in Docker/K8s Secrets):

```bash
# Basis-URL und Service-Name
set THEMIS_RANGER_BASE_URL=https://ranger.example.com
set THEMIS_RANGER_SERVICE=themisdb
set THEMIS_RANGER_BEARER=YOUR_RANGER_TOKEN

# TLS (optional)
set THEMIS_RANGER_TLS_VERIFY=1
set THEMIS_RANGER_CA_CERT=C:\\certs\\ranger_ca.pem
```

Dann importieren:

```bash
curl -X POST -H "Authorization: Bearer %THEMIS_TOKEN_ADMIN%" \
     http://localhost:8765/policies/import/ranger
```

## Testing

**Unit-Tests:**
- `tests/test_auth_middleware.cpp` — Token/Scope-basierte Authentifizierung
- `tests/test_policy_yaml.cpp` — PolicyEngine YAML-Loading und Evaluation
- `tests/test_http_policies_export.cpp` — Ranger Export/Import Endpoints

**Integration Tests:**
- Scope + Policy kombinierte Prüfung in HTTP-Tests

```bash
# Build + Test
cmake --build build --config Release

# Auth-Middleware-Tests (RBAC)
.\build\Release\themis_tests.exe --gtest_filter=AuthMiddlewareTest.*

# Policy Engine Tests (ABAC)
.\build\Release\themis_tests.exe --gtest_filter=PolicyYamlTest.*
```

## Vollständiger Security Stack

**Implementierungsstatus: ✅ Produktionsbereit**

1. ✅ **Token-basierte Authentifizierung** (RBAC mit Scopes)
2. ✅ **Policy Engine** (ABAC mit Ranger-Kompatibilität)
3. ✅ **Dual-Layer Authorization** (Scope-Check + Policy-Evaluation)
4. ✅ **IP-basierte Zugriffskontrolle** (allowed_ip_prefixes)
5. ✅ **Deny-Overrides-Semantik**
6. ✅ **Audit-Logging** aller Authorization-Entscheidungen
7. ✅ **Prometheus-Metriken** für beide Ebenen
8. ✅ **Policy Export/Import** (Ranger-kompatibel)
9. ✅ **YAML/JSON Konfiguration**
10. ✅ **PII-Reveal/Erase Scopes** mit Policy-Integration

## Roadmap

**Bereits implementiert:**
- ✅ ABAC-Schema (resource, action, subject, conditions)
- ✅ Policy-Evaluator mit Deny-Overrides
- ✅ Apache Ranger-kompatible Policies
- ✅ YAML/JSON Policy-Konfiguration
- ✅ IP-Präfix-Filtering

**Zukünftig (optional):**
- ⏳ Admin-UI für Policy-Verwaltung (Web-GUI)
- ⏳ Policy-Store in RocksDB (derzeit File-basiert)
- ⏳ Zeit-basierte Conditions (time windows)
- ⏳ Erweiterte Attribute (user groups, resource tags)
- ⏳ Policy-Caching/Performance-Optimierung
- Verschlüsselte Benutzertabelle (AES)

## Referenzen

- [Security/Compliance Review](security/security_compliance_review.md)
- [Operations Runbook](operations_runbook.md#authentication)
- [Deployment](deployment.md)
