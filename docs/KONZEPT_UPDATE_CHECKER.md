# Konzept: GitHub Update Checker Subsystem für ThemisDB

## Zusammenfassung

Dieses Dokument beschreibt das Konzept und die Implementierung eines Update-Checker-Subsystems für ThemisDB, das regelmäßig auf GitHub nach Updates prüft und diese Informationen über den HTTP-Server für Admin-Tools verfügbar macht.

## Anforderungsanalyse

### Funktionale Anforderungen

1. **Regelmäßige Update-Prüfung**
   - Periodisches Polling der GitHub Releases API
   - Konfigurierbare Prüfintervalle (Standard: 1 Stunde)
   - Manuelle Auslösung über API möglich

2. **Verfügbarkeit über HTTP-Server**
   - RESTful API-Endpoints für Admin-Tools
   - JSON-basierte Responses
   - Zustandsabfrage und Konfiguration

3. **Intelligente Versionserkennung**
   - Semantic Versioning Support
   - Erkennung kritischer Sicherheitsupdates
   - Unterscheidung zwischen Releases und Prereleases

4. **Hot-Reload für kritische Patches** (Zukünftig)
   - Automatische Update-Installation
   - Sicherheitsprüfungen vor Update
   - Rollback-Funktionalität

### Nicht-funktionale Anforderungen

1. **OOP-Design**
   - Klare Trennung der Verantwortlichkeiten
   - SOLID-Prinzipien
   - Wiederverwendbare Komponenten

2. **Thread-Safety**
   - Sichere nebenläufige Zugriffe
   - Lock-freie Algorithmen wo möglich
   - Mutex-Schutz für kritische Bereiche

3. **Performance**
   - Minimale Server-Belastung
   - Asynchrone Hintergrund-Prüfung
   - Keine Blockierung des Haupt-Servers

4. **Sicherheit**
   - Sichere API-Token-Verwaltung
   - Authentifizierung für kritische Operationen
   - Keine Secrets in Logs oder Responses

## Architektur

### Systemarchitektur

```
┌─────────────────────────────────────────────────────────┐
│                    ThemisDB Server                      │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  ┌──────────────┐         ┌─────────────────────────┐  │
│  │ HTTP Server  │────────▶│  UpdateApiHandler       │  │
│  │              │         │  - GET /api/updates     │  │
│  │              │         │  - POST /api/updates/   │  │
│  │              │         │    check                │  │
│  │              │         │  - GET /api/updates/    │  │
│  │              │         │    config               │  │
│  │              │         │  - PUT /api/updates/    │  │
│  │              │         │    config               │  │
│  └──────────────┘         └──────────┬──────────────┘  │
│                                      │                 │
│                                      ▼                 │
│                           ┌─────────────────────────┐  │
│                           │   UpdateChecker         │  │
│                           │   - Background Thread   │  │
│                           │   - Version Parsing     │  │
│                           │   - GitHub API Client   │  │
│                           │   - Status Management   │  │
│                           └──────────┬──────────────┘  │
│                                      │                 │
└──────────────────────────────────────┼─────────────────┘
                                       │
                                       │ HTTPS
                                       ▼
                            ┌──────────────────────┐
                            │  GitHub Releases API │
                            │  api.github.com      │
                            └──────────────────────┘
```

### Komponenten-Übersicht

#### 1. UpdateChecker (`utils/update_checker.h/cpp`)

**Verantwortlichkeiten:**
- Periodisches Polling der GitHub API
- Versionsverwaltung und -vergleich
- Erkennung kritischer Updates
- Statusverwaltung

**Schnittstellen:**
```cpp
class UpdateChecker {
public:
    void start();                              // Starte Background-Thread
    void stop();                               // Stoppe Background-Thread
    UpdateCheckResult checkNow();              // Sofortige Prüfung
    UpdateCheckResult getLastResult() const;   // Letztes Ergebnis
    void updateConfig(const Config& config);   // Konfiguration aktualisieren
};
```

**Design-Patterns:**
- **Singleton-ähnlich**: Über shared_ptr verwaltet
- **Observer-Pattern**: Callback für Update-Benachrichtigungen
- **Strategy-Pattern**: Austauschbare HTTP-Clients (CURL, etc.)

#### 2. Version (`utils/update_checker.h`)

**Verantwortlichkeiten:**
- Semantic Versioning Parsing
- Versionsvergleich
- Serialisierung/Deserialisierung

**Algorithmus:**
```
Version-Vergleich:
1. Vergleiche Major-Version
2. Bei Gleichheit: Vergleiche Minor-Version
3. Bei Gleichheit: Vergleiche Patch-Version
4. Bei Gleichheit: Vergleiche Prerelease
   - Kein Prerelease > Mit Prerelease
   - Lexikographischer Vergleich der Prerelease-Tags
```

#### 3. UpdateApiHandler (`server/update_api_handler.h/cpp`)

**Verantwortlichkeiten:**
- HTTP-Request-Routing
- JSON-Serialisierung
- Fehlerbehandlung
- Authentifizierung (für Config-Änderungen)

**Endpoints:**

| Methode | Pfad | Beschreibung | Auth |
|---------|------|--------------|------|
| GET | /api/updates | Update-Status abrufen | Nein |
| POST | /api/updates/check | Manuelle Prüfung auslösen | Nein |
| GET | /api/updates/config | Konfiguration abrufen | Nein |
| PUT | /api/updates/config | Konfiguration ändern | Ja |

## Implementierungsdetails

### Semantic Versioning

**Unterstützte Formate:**
- `1.2.3`
- `v1.2.3`
- `1.2.3-alpha`
- `1.2.3-beta.1`
- `1.2.3-rc.1+build.123`

**Regex-Pattern:**
```regex
^v?(\d+)\.(\d+)\.(\d+)(?:-([a-zA-Z0-9.-]+))?(?:\+([a-zA-Z0-9.-]+))?$
```

### Kritische Update-Erkennung

**Keywords für kritische Updates:**
- security
- critical
- vulnerability
- CVE-
- exploit
- patch
- urgent
- hotfix

**Algorithmus:**
```cpp
bool isCritical() const {
    string search_text = name + " " + body;
    to_lower(search_text);
    return contains_any(search_text, CRITICAL_KEYWORDS);
}
```

### Thread-Sicherheit

**Synchronisationsmechanismen:**

1. **Mutex für Konfigurationszugriff**
   ```cpp
   mutable std::mutex mutex_;
   
   UpdateCheckResult getLastResult() const {
       std::lock_guard<std::mutex> lock(mutex_);
       return last_result_;
   }
   ```

2. **Atomic für Running-Flag**
   ```cpp
   std::atomic<bool> running_{false};
   ```

3. **Thread-Safe Callback-Registrierung**
   ```cpp
   void onUpdateAvailable(std::function<void(const UpdateCheckResult&)> callback) {
       std::lock_guard<std::mutex> lock(mutex_);
       update_callback_ = std::move(callback);
   }
   ```

### HTTP-Client-Implementierung

**CURL-basiert mit Fehlerbehandlung:**

```cpp
std::variant<json, std::string> httpGet(const string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return string("Failed to initialize CURL");
    }
    
    // Konfiguration
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    
    // GitHub API Token für höhere Rate-Limits
    if (!config_.github_api_token.empty()) {
        string auth = "Authorization: token " + config_.github_api_token;
        headers = curl_slist_append(headers, auth.c_str());
    }
    
    // Request ausführen
    CURLcode res = curl_easy_perform(curl);
    
    // Cleanup und Ergebnis
    curl_easy_cleanup(curl);
    return result;
}
```

## Konfiguration

### Compile-Time

**CMake-Optionen:**
```cmake
# Update-Checker ist immer kompiliert
# CURL ist optional
find_package(CURL)
if(CURL_FOUND)
    target_compile_definitions(themis_core PUBLIC THEMIS_ENABLE_CURL)
endif()

# Version aus Projekt
target_compile_definitions(themis_core PUBLIC 
    THEMIS_VERSION_STRING="${PROJECT_VERSION}"
)
```

### Runtime

**Server-Konfiguration (YAML):**
```yaml
http_server:
  feature_update_checker: true  # Feature aktivieren
```

**Umgebungsvariablen:**
```bash
# GitHub API Token (empfohlen für höhere Rate-Limits)
export THEMIS_GITHUB_API_TOKEN=ghp_xxxxxxxxxxxxx

# Prüfintervall in Sekunden
export THEMIS_UPDATE_CHECK_INTERVAL=3600

# Automatische Updates aktivieren
export THEMIS_AUTO_UPDATE_ENABLED=false
```

## Sicherheitskonzept

### Authentifizierung

1. **Öffentliche Endpoints:**
   - `GET /api/updates` - Keine Authentifizierung
   - `POST /api/updates/check` - Keine Authentifizierung
   - `GET /api/updates/config` - Keine Authentifizierung

2. **Geschützte Endpoints:**
   - `PUT /api/updates/config` - Erfordert Admin-Token
   - `POST /api/updates/apply` (Zukünftig) - Erfordert Admin-Token + Zusatzverifizierung

### Token-Verwaltung

**Speicherung:**
- Niemals in Code hardcoded
- Nur über Umgebungsvariablen
- Niemals in Logs ausgeben

**Maskierung in Responses:**
```cpp
json UpdateCheckerConfig::toJson() const {
    json j;
    // ... andere Felder
    if (!github_api_token.empty()) {
        j["github_api_token"] = "***";  // Maskiert
    }
    return j;
}
```

### Rate-Limiting

**GitHub API Limits:**
- Ohne Token: 60 Requests/Stunde
- Mit Token: 5000 Requests/Stunde

**Empfohlene Intervalle:**
- Produktion: 1 Stunde (3600 Sekunden)
- Entwicklung: 5 Minuten (300 Sekunden)
- Testing: Manuell per API

## Fehlerbehandlung

### HTTP-Fehler

```cpp
if (http_code != 200) {
    if (http_code == 403) {
        return "Rate limit exceeded - add GitHub API token";
    } else if (http_code == 404) {
        return "Repository not found";
    } else {
        return "HTTP error: " + to_string(http_code);
    }
}
```

### Netzwerk-Fehler

```cpp
if (res != CURLE_OK) {
    string error = "CURL error: " + string(curl_easy_strerror(res));
    LOG_ERROR("Update check failed: {}", error);
    return error;
}
```

### Graceful Degradation

```cpp
#ifdef THEMIS_ENABLE_CURL
    // CURL-basierte Implementierung
#else
    return string("CURL support not enabled - cannot fetch releases");
#endif
```

## Testing-Strategie

### Unit-Tests

**Test-Kategorien:**
1. Version-Parsing
2. Version-Vergleich
3. Kritische Update-Erkennung
4. Konfigurations-Serialisierung
5. Status-Verwaltung

**Beispiel-Test:**
```cpp
TEST(UpdateCheckerTest, VersionComparison) {
    auto v1 = Version::parse("1.0.0").value();
    auto v2 = Version::parse("2.0.0").value();
    EXPECT_TRUE(v1 < v2);
}
```

### Integrations-Tests

**Test-Szenarien:**
1. Server-Start mit aktiviertem Update-Checker
2. API-Endpoint-Aufrufe
3. Konfigurationsänderungen
4. Fehlerbehandlung (kein Netzwerk, invalide Responses)

### Mocking

**GitHub API Mock:**
```json
[
  {
    "tag_name": "v1.2.0",
    "name": "Release 1.2.0",
    "body": "Security fixes and improvements",
    "published_at": "2025-01-15T10:00:00Z",
    "prerelease": false,
    "draft": false
  }
]
```

## Performance-Optimierung

### Background-Thread-Design

```cpp
void checkLoop() {
    // Initiale Prüfung
    checkNow();
    
    while (running_) {
        // Sleep in kleinen Schritten für schnelles Shutdown
        auto interval = config_.check_interval;
        auto sleep_duration = seconds(1);
        auto elapsed = seconds(0);
        
        while (running_ && elapsed < interval) {
            this_thread::sleep_for(sleep_duration);
            elapsed += sleep_duration;
        }
        
        if (running_) {
            checkNow();
        }
    }
}
```

**Vorteile:**
- Schnelles Shutdown (max 1 Sekunde Wartezeit)
- Keine CPU-Last im Idle
- Präzise Intervalle

### Caching

**Last-Result-Cache:**
- In-Memory-Speicherung
- Mutex-geschützt
- Keine Disk-I/O

## Monitoring und Logging

### Log-Levels

```cpp
// INFO: Normale Operationen
LOG_INFO("Update Checker started (interval: {}s)", interval);
LOG_INFO("Update check completed: {}", status);

// WARN: Nicht-kritische Fehler
LOG_WARN("Update check failed: {}", error);

// DEBUG: Detaillierte Informationen
LOG_DEBUG("Skipping draft release: {}", tag_name);
LOG_DEBUG("Parsed version: {}", version.toString());
```

### Metriken

**Zu erfassende Metriken:**
- Anzahl der Prüfungen
- Erfolgsrate
- Durchschnittliche Response-Zeit
- Anzahl gefundener Updates
- Anzahl kritischer Updates

## Zukünftige Erweiterungen

### Hot-Reload-Funktionalität

**Konzept:**
```
1. Download der neuen Version
2. Signatur-Verifizierung
3. Backup des aktuellen Zustands
4. Atomarer Austausch der Binaries
5. Graceful Restart
6. Rollback bei Fehlern
```

**Sicherheitsmaßnahmen:**
- GPG-Signatur-Prüfung
- Checksum-Verifikation
- Rollback-Mechanismus
- Audit-Logging aller Updates

### Update-Benachrichtigungen

**Implementierung:**
```cpp
class UpdateNotifier {
public:
    void notify(const UpdateCheckResult& result) {
        if (result.status == UpdateStatus::CRITICAL_UPDATE) {
            sendEmail(admin_email_, result);
            sendWebhook(webhook_url_, result);
            logAudit(result);
        }
    }
};
```

### Update-Historie

**Datenmodell:**
```cpp
struct UpdateHistoryEntry {
    string version;
    chrono::system_clock::time_point timestamp;
    UpdateStatus status;
    string notes;
    bool applied;
};
```

## Zusammenfassung

Das Update-Checker-Subsystem bietet eine robuste, sichere und performante Lösung für die automatisierte Überwachung von ThemisDB-Updates. Durch die Verwendung von OOP-Prinzipien, Thread-Safety und Best Practices wurde ein wartbares und erweiterbares System geschaffen.

**Hauptmerkmale:**
- ✅ OOP-Design mit klarer Trennung der Verantwortlichkeiten
- ✅ Thread-sichere Implementierung
- ✅ RESTful HTTP API
- ✅ Semantic Versioning Support
- ✅ Kritische Update-Erkennung
- ✅ Umfassende Tests und Dokumentation
- ✅ Konfigurierbar über Config und Umgebungsvariablen
- ✅ Graceful Degradation bei fehlenden Dependencies

**Vorbereitet für zukünftige Erweiterungen:**
- 📋 Hot-Reload-Funktionalität
- 📋 Update-Benachrichtigungen
- 📋 Update-Historie und Rollback
