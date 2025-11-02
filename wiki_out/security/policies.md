# Policy Engine (Ranger-inspiriert)

Die Policy-Engine ergänzt die Token/Scope-Authorisierung um feingranulare Prüfungen auf Basis von Subjekt (Benutzer), Aktion, Ressourcenpfad und optionalen Bedingungen (z. B. IP-Präfixe).

## Aktivierung & Laden

- Policies können als YAML oder JSON hinterlegt werden.
  - Bevorzugte Dateien: `config/policies.yaml` oder `config/policies.yml`
  - Fallback: `config/policies.json`
- Der Server lädt diese Datei beim Start automatisch (optional). Existiert keine Datei, gilt bis zum Setzen von Policies: Default-Allow.
- Wenn Policies vorhanden sind und keine Policy passt, wird der Zugriff per Default-Deny blockiert (Secure-by-Default).

> Hinweis: Hot-Reload per API folgt in einer späteren Version. Bis dahin: Datei anpassen und Server neu starten.

## Schema (MVP)

- `id` (string): Eindeutige Policy-ID
- `name` (string): Beschreibung
- `subjects` (array<string>): Benutzer-IDs oder `"*"` (Wildcard)
- `actions` (set<string>): z. B. `read`, `write`, `delete`, `query`, `config.read`, `metrics.read`, `cdc.read`, `cdc.admin`, `vector.search`, `vector.write`, `"*"`
- `resources` (array<string>): Pfadpräfixe, z. B. `"/entities/"`, `"/vector/search"`, `"/"` (alles)
- `effect` ("allow" | "deny"): Effekt der Policy
- `allowed_ip_prefixes` (optional array<string>): Wenn gesetzt, muss die Client-IP (aus `X-Forwarded-For` oder `X-Real-IP`) mit mindestens einem Präfix übereinstimmen

Beispiel (YAML):

```yaml
- id: hr-allow-internal
  name: HR nur im Intranet zulassen
  subjects: ["*"]
  actions: [read, write, delete]
  resources: ["/entities/hr:"]
  allowed_ip_prefixes: ["10.", "192.168.", "172.16.", "172.17.", "172.18."]
  effect: allow
```

Beispiel (JSON):

```
{
  "id": "hr-allow-internal",
  "name": "HR nur im Intranet zulassen",
  "subjects": ["*"],
  "actions": ["read", "write", "delete"],
  "resources": ["/entities/hr:"],
  "allowed_ip_prefixes": ["10.", "192.168.", "172.16.", "172.17.", "172.18."] ,
  "effect": "allow"
}
```

## Action-Mapping (Server)

Die wichtigsten Endpunkte lösen folgende `actions` aus (neben Scope-Pflicht):

- `GET /metrics` → `metrics.read`
- `GET/POST /config` → `config.read` / `config.write`
- `GET /entities/:key` → `read`
- `PUT /entities` / `DELETE /entities/:key` → `write` / `delete`
- `POST /query` → `query`
- `POST /vector/search` → `vector.search`
- `POST /vector/batch_insert`, `DELETE /vector/by-filter` → `vector.write`
- `GET /changefeed`, `GET /changefeed/stream` → `cdc.read`
- `GET /changefeed/stats`, `POST /changefeed/retention` → `cdc.admin`

Zusätzlich ist weiterhin ein gültiger Scope am Token erforderlich (z. B. `data:read`, `data:write`, `metrics:read`, ...).

## Metriken

Im Prometheus-Endpoint `/metrics` verfügbar:

- `vccdb_policy_eval_total`
- `vccdb_policy_allow_total`
- `vccdb_policy_deny_total`

## Start-Policies (Baseline)

Wir liefern eine Basis-Datei unter `config/policies.json` mit (du kannst sie 1:1 als `policies.yaml` umsetzen):

- Admin: `admin` darf alles (`actions: ["*"]`, `resources: ["/"]`)
- Readonly: `readonly` darf Metriken, Config-Read, Daten-Reads, Query, Vector-Search, CDC-Read auf passenden Pfaden
- Analyst: `analyst` (+ analyst1/analyst2) darf Query/Vector-Search/Data-Read
- HR-Bereich: erlauben aus internen Netzen (`10.*`, `192.168.*`, `172.16/12`) und sonst explizit deny

## Tokens (Beispiel)

Die folgenden Umgebungsvariablen werden beim Start eingelesen:

- `THEMIS_TOKEN_ADMIN` → Benutzer `admin` mit breiten Scopes
- `THEMIS_TOKEN_READONLY` → Benutzer `readonly` (Read-Scopes)
- `THEMIS_TOKEN_ANALYST` → Benutzer `analyst` (Read-Scopes, inkl. Vector/Query)

Beispiel (PowerShell):

```powershell
$env:THEMIS_TOKEN_ADMIN = "<geheim>"
$env:THEMIS_TOKEN_READONLY = "<readonly-token>"
$env:THEMIS_TOKEN_ANALYST = "<analyst-token>"
```

> Wichtig: Scopes im Token und Policies müssen zusammenpassen. Beispiel: `vector.search` bedingt i. d. R. den Scope `data:read`.

## Apache Ranger Integration (optional)

Die interne Policy-Engine deckt das operative Enforcen ab (schnell, lokal, ohne externen Hop). Eine Integration mit Apache Ranger kann sich dennoch lohnen – als optionales Management- und Governance-Frontend.

Wann Ranger sinnvoll ist:

- Zentrale Verwaltung/Review von Policies (UI, Workflows, Versionierung, Audit)
- Einheitliche Governance über mehrere Systeme hinweg (Data Lake, DBs, Services)
- Nutzung von Tag-basierten Policies und fortgeschrittenen ABAC-Mustern

Kosten/Trade-offs:

- Zusätzliche Infrastruktur (Ranger Admin/DB), Betrieb und AuthN/SSO-Anbindung
- Modellsynchronisation (Ranger-Objektmodell vs. unsere Pfad-/Action-Logik)
- Fehler- und Latenz-Domäne, wenn Policies live remote abgefragt würden

Empfohlener Ansatz (Snapshot-Sync):

1) Ranger bleibt Management-Ebene. Policies werden periodisch über die Ranger-REST-API gelesen und in unsere lokale `policies.json` übersetzt (Snapshot).
2) Enforcen erfolgt weiterhin lokal – robust gegen Netzausfälle und mit konstanter Latenz.
3) Optional: manuelles Import/Export (Admin-API), sowie Hintergrund-Sync (z. B. alle 60s) mit ETag/Version.

Geplante Minimalintegration (MVP):

- Import/Export-Adapter: `POST /policies/import/ranger`, `GET /policies/export/ranger`
- Übersetzung Ranger → intern (Beispiel):
  - Ranger „resource.path" → `resources` (Pfad-Präfix)
  - Ranger „users/groups" → `subjects`
  - Ranger „accessTypes" → `actions` (Mapping, z. B. `read`/`write`/`admin` zu unseren Actions)
  - Ranger „allow/deny" → `effect`
- Hintergrund-Sync (optional): Poll aus Ranger Admin API mit Service-Name-Filter, Schreibschutz via ETag/Policy-Versionen

Bewährte Praxis:

- Lokales Enforcen mit synchronisiertem Snapshot. Remote-Live-Enforcen vermeiden (Latenz, SPOF).
- Ein dedizierter Service-Name in Ranger für ThemisDB (z. B. `themisdb-prod`).
- Einfache, dokumentierte Action-Mappings (siehe oben). Feingranularere Attribute/Tags können in einer zweiten Ausbaustufe ergänzt werden.

Sicherheit & Betrieb:

- Import-Adapter mit mTLS/OAuth2 gegen Ranger absichern.
- Änderungen auditieren (z. B. in unserem Audit-Logger + Ranger Audit).
- Fallback: Wenn Ranger nicht erreichbar ist, wird die letzte gültige lokale Policy weiter genutzt.

Fazit: Ranger ist nicht erforderlich, bringt aber als optionales Management deutliche Vorteile in Enterprise-Umgebungen. Der empfohlene Weg ist ein lesender Sync mit lokalem Enforcen.

**⚠️ Production-Hinweis:** Die aktuelle Ranger-Adapter-Implementierung ist funktional für Dev/Demo-Umgebungen. Für Production-Deployments mit hoher Last wird empfohlen, Connection-Pooling, Retry-Logic und Timeout-Konfiguration hinzuzufügen. Details siehe `CODE_AUDIT_MOCKUPS_STUBS.md`.

### API-Details & Absicherung

- Import: `POST /policies/import/ranger` (erfordert Scope `admin` sowie passende Policy)
  - Liest Ranger-Policies via REST (Bearer-Token) und konvertiert sie nach intern; Ergebnis wird als Snapshot in `config/policies.json` gespeichert.
- Export: `GET /policies/export/ranger` (erfordert Scope `admin`)
  - Gibt ein minimales, Ranger-ähnliches JSON aus (nicht 1:1 Schema), für Review/Debugging.

Sicherheit (ENV-Variablen, Beispiel – Secrets gehören in sichere Secret-Stores):

```powershell
# Ranger Endpoint + Service
$env:THEMIS_RANGER_BASE_URL = "https://ranger.example.com"
$env:THEMIS_RANGER_SERVICE = "themisdb-prod"
# Optional: Pfad (Default: /service/public/v2/api/policy)
$env:THEMIS_RANGER_POLICIES_PATH = "/service/public/v2/api/policy"

# AuthN
$env:THEMIS_RANGER_BEARER = "<ranger-api-token>"  # Authorization: Bearer

# TLS
$env:THEMIS_RANGER_TLS_VERIFY = "1"                # 1=verify (Default), 0=skip (nur Test!)
$env:THEMIS_RANGER_CA_CERT    = "C:\\certs\\ca.pem"  # optional eigenes CA-Bundle
# mTLS (optional)
$env:THEMIS_RANGER_CLIENT_CERT = "C:\\certs\\client.crt"
$env:THEMIS_RANGER_CLIENT_KEY  = "C:\\certs\\client.key"

# Timeouts & Retry (optional, defaults shown)
$env:THEMIS_RANGER_CONNECT_TIMEOUT_MS = "5000"     # Verbindungsaufbau-Timeout
$env:THEMIS_RANGER_REQUEST_TIMEOUT_MS = "15000"    # Gesamt-Request-Timeout
$env:THEMIS_RANGER_MAX_RETRIES        = "2"        # Anzahl Wiederholungen bei 5xx/Netzfehlern
$env:THEMIS_RANGER_RETRY_BACKOFF_MS   = "500"      # Initialer Backoff (exponentiell)
```

Hinweise:

- Standard ist TLS-Verifikation aktiv. Abschalten nur in Testumgebungen.
- Admin-geschützte Endpunkte zusätzlich durch Policies abgesichert (action `admin`).
- Import erfolgt als Snapshot; Enforcen bleibt lokal → keine Ranger-Latenz im Hot-Path.
