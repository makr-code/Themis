# RBAC & Authorization (MVP)

Diese Seite beschreibt die API-Token-basierte Zugriffskontrolle in ThemisDB. MVP-Version nutzt statische Token mit Scopes; später erweitert um ABAC (Ranger-inspiriert).

## Konzept

- **Token-basiert:** API-Clients senden `Authorization: Bearer <token>` Header
- **Scopes:** Jeder Token hat einen Satz von Scopes (z. B. `admin`, `config:write`, `cdc:read`, `metrics:read`)
- **Endpoint-Schutz:** Sensitive Endpunkte prüfen erforderliche Scopes
- **Audit-Logs:** Verweigerte Zugriffe werden geloggt
- **Metriken:** Prometheus-Zähler für Autorisierungsergebnisse

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

Datenendpunkte (`/entities`, `/query`, `/graph`, `/vector`) sind zunächst offen; optionale Aktivierung via Feature-Flag `require_data_auth`.

## Metriken

Prometheus `/metrics` enthält:

- `themis_authz_success_total` — Erfolgreiche Autorisierungen (Label: `user_id`, `scope`)
- `themis_authz_denied_total` — Verweigerte Zugriffe (Label: `user_id`, `scope`, `reason`)
- `themis_authz_invalid_token_total` — Ungültige/fehlende Token

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

## Roadmap: ABAC & Policy-Engine

Mittelfristig (Sprint C/Q4):

- ABAC-Schema (resource, action, subject attributes)
- Policy-Store (RocksDB), Evaluator, Caching
- Apache Ranger-kompatible Konzepte (Policies, Deny-Overrides)
- Admin-UI für Policy-Verwaltung

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

## Testing

Unit-Tests: `tests/test_auth_middleware.cpp`

```bash
# Build + Test
cmake --build build --config Release
.\build\Release\themis_tests.exe --gtest_filter=AuthMiddlewareTest.*
```

## Referenzen

- [Security/Compliance Review](security/security_compliance_review.md)
- [Operations Runbook](operations_runbook.md#authentication)
- [Deployment](deployment.md)
