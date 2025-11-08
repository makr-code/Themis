# ThemisDB Audit API – Implementierung & Hardening ✅

**Datum:** 7. November 2025  
**Status:** REST-API implementiert, Hardening (URL-Decoding, erweiterter ISO8601 Parser, Rate-Limiting) aktiv, Tests grün

---

## Implementierte Komponenten

### 1. C++ Backend (themis_server)

#### AuditApiHandler (`include/server/audit_api_handler.h`, `src/server/audit_api_handler.cpp`)
**Funktionalität:**
- Liest und dekodiert verschlüsselte Audit-Logs aus JSONL-Datei (`data/logs/audit.jsonl`)
- Entschlüsselt encrypt-then-sign Payloads mit FieldEncryption
- Filtert Logs nach mehreren Kriterien
- Paginierung (1-1000 Einträge pro Seite)
- CSV-Export

**Strukturen:**
```cpp
struct AuditLogEntry {
    int64_t id, timestamp_ms;
    std::string user, action, entity_type, entity_id;
    std::string old_value, new_value;
    bool success;
    std::string ip_address, session_id, error_message;
};

struct AuditQueryFilter {
    int64_t start_ts_ms, end_ts_ms;
    std::string user, action, entity_type, entity_id;
    bool success_only;
    int page, page_size;
};
```

**Methoden:**
- `queryAuditLogs(filter)` → JSON mit entries[], totalCount, page, pageSize, hasMore
- `exportAuditLogsCsv(filter)` → CSV-String mit escaped Feldern

#### HTTP-Endpunkte (http_server.cpp)

**GET /api/audit**
- Query-Parameter: `start`, `end`, `user`, `action`, `entity_type`, `entity_id`, `success`, `page`, `page_size`
- Antwort: `application/json`
```json
{
  "entries": [...],
  "totalCount": 1234,
  "page": 1,
  "pageSize": 100,
  "hasMore": true
}
```

**GET /api/audit/export/csv**
- Gleiche Query-Parameter
- Antwort: `text/csv` mit Datei-Download

**Features (aktuell):**
- ISO 8601 Datum-Parsing: Unterstützung für `YYYY-MM-DDTHH:MM:SS`, optionale Millisekunden (`.fff`), sowie Zeitzonen `Z` oder Offset `±HH:MM` (Normalisierung nach UTC)
- Epoch-Millis als Alternative (numerischer Parameterwert)
- Vollständiges URL-Decoding (Percent-Encoding inkl. `+` → Leerzeichen)
- Case-insensitive Filterung der Erfolg-Parameter (`success=true|1|yes`)
- Automatische Sortierung (neueste zuerst)
- CSV-Export mit konfigurierbarer Page-Size (bis 10.000)
- Rate-Limiting pro Minute pro (Route + Authorization-Token oder anonym) mit Retry-After
- Integration mit Audit-Logger (encrypt-then-sign) Infrastruktur

---

### 2. .NET Frontend (Themis.AuditLogViewer)

#### ThemisApiClient.cs
- REST-Client für themis_server
- `GetAuditLogsAsync(filter)` → ApiResponse<AuditLogResponse>
- `ExportAuditLogsToCsvAsync(filter)` → ApiResponse<byte[]>
- Query-String-Builder mit URL-Encoding
- Konfigurierbar via `appsettings.json`

#### MockThemisApiClient.cs
- Generiert 1234 simulierte Audit-Einträge
- Für Tests ohne laufenden Server
- Gleiche Schnittstelle wie echter Client

#### WPF-Anwendung
- MVVM mit DataGrid (11 Spalten)
- Filter: Datum, User, Action, Entity, Success-Only
- Paginierung mit Vor/Zurück-Buttons
- CSV-Export mit SaveFileDialog
- Loading-Indikator, Statusleiste
- Dependency Injection (Microsoft.Extensions.DI)

---

## Build-Status (Kurz)

- C++ Server: Endpunkte in `HttpServer` integriert, Unit- und HTTP-Tests vorhanden (siehe `tests/test_http_audit.cpp`).
- .NET Tools: WPF Viewer vorhanden unter `tools/Themis.AuditLogViewer` (optionale Nutzung).

---

## Dateistruktur

```
c:\VCC\themis\
├── include\server\
│   ├── audit_api_handler.h          ✅ NEU
│   └── http_server.h                ✅ ERWEITERT
├── src\server\
│   ├── audit_api_handler.cpp        ✅ NEU (188 Zeilen)
│   └── http_server.cpp              ✅ ERWEITERT (+170 Zeilen)
├── tools\
│   ├── Themis.sln
│   ├── README.md
│   ├── STATUS.md
│   ├── MOCK_MODE.md
│   ├── Themis.AdminTools.Shared\
│   │   ├── ApiClient\
│   │   │   ├── ThemisApiClient.cs          ✅ REST-Client
│   │   │   └── MockThemisApiClient.cs       ✅ Mock
│   │   └── Models\
│   │       ├── AuditLogModels.cs           ✅ DTOs
│   │       └── Common.cs                   ✅ Shared
│   └── Themis.AuditLogViewer\
│       ├── App.xaml + .cs                  ✅ DI-Setup
│       ├── appsettings.json                ✅ Config
│       ├── ViewModels\
│       │   └── MainWindowViewModel.cs      ✅ MVVM
│       └── Views\
│           └── MainWindow.xaml + .cs       ✅ UI
└── CMakeLists.txt                          ✅ ERWEITERT
```

---

## Integration in themis_server

### Initialisierung (http_server.cpp – Konstruktor)
```cpp
// Audit Logger (neu)
themis::utils::AuditLoggerConfig audit_cfg;
audit_cfg.log_path = "data/logs/audit.jsonl";
audit_logger_ = std::make_shared<themis::utils::AuditLogger>(
    field_enc, pki_client, audit_cfg);

// Audit API Handler (neu)
audit_api_ = std::make_unique<themis::server::AuditApiHandler>(
    field_enc, pki_client, audit_cfg.log_path);
```

### Routing (http_server.cpp)
```cpp
if (path_only == "/api/audit" && method == http::verb::get)
    return Route::AuditQueryGet;
if (path_only == "/api/audit/export/csv" && method == http::verb::get)
    return Route::AuditExportCsvGet;
```

### Handler-Dispatch (http_server.cpp)
```cpp
case Route::AuditQueryGet:
    response = handleAuditQuery(req);
    break;
case Route::AuditExportCsvGet:
    response = handleAuditExportCsv(req);
    break;
```

---

## Funktionsweise

### Workflow: Audit-Log-Eintrag → UI

```
1. Anwendung → AuditLogger::logEvent(event)
2. AuditLogger → Verschlüsselt Payload (FieldEncryption)
3. AuditLogger → Signiert Hash mit PKI
4. AuditLogger → Schreibt JSONL-Zeile in data/logs/audit.jsonl
   {
     "ts": 1730476800000,
     "category": "AUDIT",
     "payload": { "ciphertext_b64": "...", "iv_b64": "...", "tag_b64": "..." },
     "signature": { "sig_b64": "...", "cert_serial": "..." }
   }

5. WPF-App → GET /api/audit?start=...&user=admin&page=1
6. HttpServer::handleAuditQuery() → AuditApiHandler::queryAuditLogs()
7. AuditApiHandler → Liest JSONL-Datei Zeile für Zeile
8. AuditApiHandler → Entschlüsselt payload mit FieldEncryption
9. AuditApiHandler → Parsed JSON-Event aus entschlüsseltem Payload
10. AuditApiHandler → Filtert nach Query-Parametern
11. AuditApiHandler → Sortiert nach Timestamp (neueste zuerst)
12. AuditApiHandler → Paginiert (z.B. Zeilen 1-100)
13. HttpServer → Gibt JSON zurück
14. WPF-App → Zeigt Einträge im DataGrid
```

---

## Beispiel-Requests

### Alle Logs der letzten 7 Tage
```
GET /api/audit?start=2025-10-25T00:00:00&end=2025-11-01T23:59:59&page=1&page_size=100
```

### Nur fehlerhafte Aktionen von User "admin"
```
GET /api/audit?user=admin&success=false&page=1
```

### CSV-Export aller CREATE-Aktionen
```
GET /api/audit/export/csv?action=CREATE&page_size=10000
```

---

## Nächste Schritte

### Sofort testbar (Mock-Modus)
```powershell
cd c:\VCC\themis\tools\Themis.AuditLogViewer
# Folge Anleitung in MOCK_MODE.md
dotnet run
```

### Integration-Test mit echtem Server
1. **Server starten:**
   ```powershell
   cd c:\VCC\themis\build
   .\Release\themis_server.exe
   ```

2. **Audit-Logs generieren:**
   - Führe beliebige CRUD-Operationen aus (PUT /entities, POST /query, etc.)
   - Logs werden automatisch in `data/logs/audit.jsonl` geschrieben

3. **WPF-App konfigurieren:**
   ```json
   // tools\Themis.AuditLogViewer\appsettings.json
   {
     "ThemisServer": {
       "BaseUrl": "http://localhost:8080"
     }
   }
   ```

4. **App starten:**
   ```powershell
   cd c:\VCC\themis\tools\Themis.AuditLogViewer
   dotnet run
   ```

5. **Testen:**
   - Filter anwenden (Datum, User, Action)
   - Paginierung durchklicken
   - CSV exportieren

---

## Bekannte Limitierungen

### C++ API
- Authentifizierung/Autorisierung: Wenn Tokens via Env gesetzt sind, verlangen die Endpunkte den Scope `audit:read`. Der ADMIN‑Token erhält diesen Scope automatisch. (Readonly-Token kann optional erweitert werden.)
- Rate-Limiting ist aktuell statisch pro Minute (fester 60s Zeitschlitz, keine gleitenden Fenster)
- Page-Size für JSON Query auf 1000 begrenzt; CSV auf 10.000 – kein Streaming-Paging für große Exporte
- ISO 8601 Parser deckt kein Wochen-/Ordinaldatum und keine rein Minuten/UTC-Abkürzungen ab (nur Basis-Format + Offset)
- URL-Decoder deckt kein Unicode-Normalisierungs-Handling ab (Byte-orientiert)

### .NET App
- Keine Unit-Tests
- Keine Internationalisierung (nur Deutsch)
- Export auf 10k Einträge limitiert
- Keine Fehler-Wiederholung bei Netzwerkfehlern

---

## Verbesserungsideen

### Kurzfristig
- Rollen/Scopes feinjustieren (z. B. `readonly` optional mit `audit:read`)
- Dynamisches Rate-Limiting (gleitende Fenster / Token-Leaky-Bucket)
- Erweiterter ISO 8601 Parser (Edge Cases, Validierung, Reject invalid)
- Einheitliches Error-Handling mit strukturiertem Fehlerobjekt (Fehlercode, Tracking-ID)

### Auth & Scopes (Beispiel)

- ADMIN‑Token per Umgebung setzen (erhält `audit:read`):
  - Windows PowerShell:
    - `$env:THEMIS_TOKEN_ADMIN = "<geheimer-token>"`
  - Linux/macOS:
    - `export THEMIS_TOKEN_ADMIN=<geheimer-token>`
- Request mit Bearer Header:
  - `Authorization: Bearer <geheimer-token>`

### Mittelfristig
- WebSocket/SSE für Echtzeit-Updates
- Elasticsearch / Columnar Secondary Store für schnelle Filterung
- Grafische Auswertungen (Charts, Heatmaps)
- Excel-Export zusätzlich zu CSV

### Langfristig
- KI-basierte Anomalie-Erkennung
- SIEM-Integration (Splunk, ELK-Stack)
- Multi-Tenant-Fähigkeit inkl. Mandanten-Trennung der Audit-Dateien
- Audit-Log-Kompression / Rotation (z. B. zstd + Zeitindex)

---

## Rate-Limiting Details

Die Audit-Endpunkte nutzen ein einfaches Fenster-basiertes Limiting:

| Merkmal | Wert |
|---------|------|
| Konfiguration | Env `THEMIS_AUDIT_RATE_LIMIT` (Standard: 100) |
| Fenster | 60 Sekunden (fixe Zeitscheiben) |
| Schlüssel | `route + ':' + Authorization-Header` oder `anon` |
| Antworten bei Limit | HTTP 429 mit Header `Retry-After: 60`, `X-RateLimit-Limit`, `X-RateLimit-Remaining` |

Beispiel:
```
GET /api/audit?page=1&page_size=10
Authorization: Bearer <token>
→ Nach >N Requests innerhalb einer Minute: 429
```

Erweiterungsideen:
- Sliding Window / Token Bucket für gleichmäßigere Verteilung
- Metriken (Prometheus) für aktuelle Bucket-Auslastung
- Differenzierte Limits für JSON vs. CSV Export

## URL-Decoding

Aktuelle Implementierung ersetzt:
- `+` → Leerzeichen
- `%HH` → Byte (hexadezimal)

Nicht unterstützt:
- Mehrfache Kodierung (`%252F` → `%2F` → `/` nur einstufig)
- UTF-8 Validierung – Bytes werden roh übernommen

## Zeitstempel-Parsing

Akzeptierte Formen:
```
1730860000000          # Epoch Millis
2025-11-07T10:15:30Z   # Zulu
2025-11-07T10:15:30.123Z
2025-11-07T11:15:30+01:00
2025-11-07T09:15:30-01:00
```

Fehlerhafte oder nicht erkannte Eingaben → 0 (Aktuell stillschweigend; zukünftige Änderung: 400 Bad Request)

## Sicherheit & Scopes

Benötigter Scope für beide Endpunkte: `audit:read`.
Aktuell vergebene Scopes (Umgebungsvariablen):

| Token | Env Variable | Standard-Scopes |
|-------|--------------|-----------------|
| Admin | THEMIS_TOKEN_ADMIN | admin, config:read, config:write, cdc:read, cdc:admin, metrics:read, data:read, data:write, audit:read |
| ReadOnly | THEMIS_TOKEN_READONLY | metrics:read, config:read, data:read, cdc:read, audit:read |
| Analyst | THEMIS_TOKEN_ANALYST | metrics:read, data:read, cdc:read |

Hinweis: Analyst-Token erhält aktuell keinen `audit:read` Scope – bewusst getrennt.

Header-Beispiel:
```
Authorization: Bearer <ADMIN_TOKEN>
```

## Fehlerantworten (aktuell)

| Status | Grund |
|--------|-------|
| 400 | Ungültige Parameter (z. B. page < 1) |
| 401 | Fehlender oder ungültiger Authorization-Header |
| 403 | Token ohne erforderlichen Scope |
| 429 | Rate Limit erreicht |
| 500 | Interner Fehler beim Lesen/Entschlüsseln |

Struktur (Beispiel 429):
```json
{"error":true,"message":"Rate limit exceeded","status_code":429}
```

Geplante Vereinheitlichung: `{ "error": { "code": "rate_limit", "message": "...", "retry_after": 60 } }`

## Beispiel für erweiterten Query mit Offset und URL-Encoding

```
GET /api/audit?user=alice%2Badmin&action=VIEW%2FACCESS&start=1969-12-31T00:00:00Z&end=2100-01-01T00:00:00%2B02:00&page=1&page_size=10
```

## Environment Konfiguration Zusammenfassung

| Variable | Bedeutung |
|----------|-----------|
| THEMIS_AUDIT_RATE_LIMIT | Requests pro Minute (0 = kein Limit) |
| THEMIS_TOKEN_ADMIN | Admin-Token mit umfassenden Scopes |
| THEMIS_TOKEN_READONLY | Optionaler ReadOnly-Token |

## Tests

`tests/test_http_audit.cpp` enthält u. a.:
- `QueryReturnsSingleEntry` – Basisfunktion
- `CsvExportReturnsHeaderAndRow` – CSV-Export
- `UrlDecodingAndIso8601RangeAndRateLimit` – URL-Decoding, ISO8601 mit Offset, Rate-Limit (429)

## Nächste Dokumentationsschritte

- OpenAPI (`openapi/openapi.yaml`) um 429-Schema erweitern
- Einheitliches Fehlerobjekt spezifizieren
- Beispiel für Audit-spezifische Governance-Header aufnehmen
- Performance-Hinweise (große CSV Exporte vs. JSON Paging)

---

**Erstellt & gepflegt:** VS Code Copilot – Aktualisiert nach Hardening

---

**Erstellt:** VS Code Copilot  
**Zusammenfassung:** themis_server REST API vollständig implementiert und getestet (Build ✅)
