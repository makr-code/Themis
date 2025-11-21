# Audit Logging - Security Events, Hash Chain & SIEM Integration

Umfassende Audit-Logging-Funktionalität mit Manipulationsschutz und SIEM-Integration.

## Übersicht

Das erweiterte Audit-Logging-System bietet:
- **65 Security Event Types**: Granulare Klassifizierung von Sicherheitsereignissen
- **Hash Chain**: Manipulationssichere Verkettung der Log-Einträge (Merkle-ähnliche Struktur)
- **SIEM Integration**: Automatische Weiterleitung an Syslog oder Splunk
- **Severity Levels**: HIGH/MEDIUM/LOW basierend auf Event-Typ
- **Chain Verification**: Automatische Integritätsprüfung beim Start

## Security Event Types

### Authentication Events
- `LOGIN_SUCCESS` - Erfolgreiche Authentifizierung
- `LOGIN_FAILED` - Fehlgeschlagener Login-Versuch
- `LOGOUT` - Benutzer-Logout
- `TOKEN_CREATED` - API-Token generiert
- `TOKEN_REVOKED` - API-Token widerrufen
- `UNAUTHORIZED_ACCESS` - Versuch auf geschützte Ressource ohne Autorisierung

### Authorization Events
- `PERMISSION_DENIED` - Fehlende Berechtigung
- `PRIVILEGE_ESCALATION_ATTEMPT` - Versuch der Rechteausweitung (HIGH severity)
- `ROLE_CHANGED` - Rolle eines Benutzers geändert
- `SCOPE_GRANTED` - Berechtigung erteilt
- `SCOPE_REVOKED` - Berechtigung entzogen

### Key Management Events
- `KEY_CREATED` - Verschlüsselungsschlüssel erstellt
- `KEY_ROTATED` - Schlüsselrotation durchgeführt
- `KEY_DELETED` - Schlüssel gelöscht
- `KEY_ACCESS` - Zugriff auf Schlüssel

### Data Access Events
- `DATA_READ` - Datenzugriff (lesend)
- `DATA_WRITE` - Daten geschrieben/geändert
- `DATA_DELETE` - Daten gelöscht
- `BULK_EXPORT` - Massenexport von Daten

### PII Events
- `PII_ACCESSED` - Zugriff auf personenbezogene Daten
- `PII_REVEALED` - PII entschlüsselt/angezeigt
- `PII_ERASED` - PII gemäß DSGVO/GDPR gelöscht

### Security Events (HIGH severity)
- `BRUTE_FORCE_DETECTED` - Brute-Force-Angriff erkannt
- `RATE_LIMIT_EXCEEDED` - Rate Limit überschritten
- `SUSPICIOUS_ACTIVITY` - Verdächtige Aktivität
- `INTEGRITY_VIOLATION` - Manipulationsversuch erkannt

### Configuration Events
- `CONFIG_CHANGED` - Konfiguration geändert
- `POLICY_UPDATED` - Verschlüsselungsrichtlinie aktualisiert
- `ENCRYPTION_SCHEMA_CHANGED` - Verschlüsselungsschema geändert

### System Events
- `SERVER_STARTED` - Server gestartet
- `SERVER_STOPPED` - Server gestoppt
- `BACKUP_CREATED` - Backup erstellt
- `RESTORE_COMPLETED` - Wiederherstellung abgeschlossen

## Konfiguration

### Environment Variables

```bash
# Hash Chain aktivieren
export THEMIS_AUDIT_ENABLE_HASH_CHAIN=true
export THEMIS_AUDIT_CHAIN_STATE_FILE=/var/lib/themis/audit_chain.json

# SIEM Integration (Syslog)
export THEMIS_AUDIT_ENABLE_SIEM=true
export THEMIS_AUDIT_SIEM_TYPE=syslog
export THEMIS_AUDIT_SIEM_HOST=192.168.1.100
export THEMIS_AUDIT_SIEM_PORT=514

# SIEM Integration (Splunk HEC)
export THEMIS_AUDIT_SIEM_TYPE=splunk
export THEMIS_AUDIT_SIEM_HOST=splunk.example.com
export THEMIS_AUDIT_SIEM_PORT=8088
export THEMIS_AUDIT_SPLUNK_TOKEN=your-hec-token-here
```

### Programmatische Konfiguration

```cpp
#include "utils/audit_logger.h"

using namespace themis::utils;

AuditLoggerConfig config;
config.log_path = "/var/log/themis/audit.jsonl";
config.enable_encryption = true;
config.master_key = /* ... */;

// Hash Chain aktivieren
config.enable_hash_chain = true;
config.chain_state_file = "/var/lib/themis/audit_chain.json";

// SIEM Syslog
config.enable_siem = true;
config.siem_type = "syslog";
config.siem_host = "192.168.1.100";
config.siem_port = 514;

AuditLogger logger(config);
```

## Verwendung

### Security Events loggen

```cpp
// Login-Fehler
logger.logSecurityEvent(
    SecurityEventType::LOGIN_FAILED,
    "user@example.com",
    "/api/login",
    {{"reason", "invalid_credentials"}, {"ip", "203.0.113.42"}}
);

// Privilege Escalation Versuch (HIGH severity)
logger.logSecurityEvent(
    SecurityEventType::PRIVILEGE_ESCALATION_ATTEMPT,
    "alice@example.com",
    "/api/admin/users",
    {{"attempted_role", "admin"}, {"current_role", "user"}}
);

// Schlüsselrotation
logger.logSecurityEvent(
    SecurityEventType::KEY_ROTATED,
    "system",
    "encryption_key_v2",
    {{"old_key_id", "key_001"}, {"new_key_id", "key_002"}}
);

// PII-Zugriff
logger.logSecurityEvent(
    SecurityEventType::PII_ACCESSED,
    "operator@example.com",
    "users/12345/email",
    {{"field", "email"}, {"purpose", "support_request"}}
);
```

### Hash Chain Integrität prüfen

```cpp
// Automatisch beim Start
AuditLogger logger(config); // Führt Integritätsprüfung durch

// Manuell
bool is_valid = logger.verifyChainIntegrity();
if (!is_valid) {
    THEMIS_ERROR("Audit log tampering detected!");
    // Alarm auslösen, Incident Response
}

// Chain State abrufen
auto state = logger.getChainState();
std::cout << "Last hash: " << state["last_hash"].get<std::string>() << "\n";
std::cout << "Entry count: " << state["entry_count"].get<uint64_t>() << "\n";
```

## Hash Chain Mechanismus

### Funktionsweise

Jeder Log-Eintrag enthält den Hash des vorherigen Eintrags:

```
Entry 1: prev_hash = 000...000 (genesis), hash = SHA256(genesis + entry_1_json)
Entry 2: prev_hash = hash_1,     hash = SHA256(hash_1 + entry_2_json)
Entry 3: prev_hash = hash_2,     hash = SHA256(hash_2 + entry_3_json)
...
```

### Log-Format mit Hash Chain

```json
{
  "timestamp": 1704067200000,
  "event_type": "LOGIN_FAILED",
  "user_id": "alice@example.com",
  "resource": "/api/login",
  "details": {"reason": "invalid_credentials"},
  "severity": "HIGH",
  "prev_hash": "a1b2c3d4...",
  "chain_entry": 42
}
```

### Manipulationsschutz

- **Änderung eines Eintrags**: Der Hash ändert sich → alle nachfolgenden Einträge ungültig
- **Löschen eines Eintrags**: Chain-Bruch erkennbar (fehlende Entry-Nummer)
- **Einfügen eines Eintrags**: prev_hash stimmt nicht mit vorherigem Entry überein
- **Neuordnung**: Timestamps und Entry-Nummern inkonsistent

### Chain State File

```json
{
  "last_hash": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
  "entry_count": 1234,
  "last_timestamp_ms": 1704067200000
}
```

Speicherort: `/var/lib/themis/audit_chain.json` (konfigurierbar)

## SIEM Integration

### Syslog (RFC 5424)

Standard-Syslog über UDP (Port 514):

```
<134>1 2024-01-01T12:00:00Z themisdb themis-audit - - - {"event_type":"LOGIN_FAILED",...}
```

- **Facility**: 16 (local use 0)
- **Severity**: 6 (informational)
- **Format**: RFC 5424 mit JSON-Payload

### Splunk HEC

HTTP Event Collector (Port 8088):

```bash
POST /services/collector/event HTTP/1.1
Host: splunk.example.com:8088
Authorization: Splunk your-hec-token
Content-Type: application/json

{
  "time": 1704067200,
  "event": {
    "event_type": "LOGIN_FAILED",
    "user_id": "alice@example.com",
    ...
  }
}
```

**Hinweis**: Splunk HEC-Implementierung erfordert libcurl (aktuell TODO).

### SIEM-Queries

**Splunk**:
```spl
index=security sourcetype=themis_audit event_type=LOGIN_FAILED
| stats count by user_id
| where count > 5
```

**ELK Stack (Elasticsearch)**:
```json
{
  "query": {
    "bool": {
      "must": [
        {"term": {"event_type": "LOGIN_FAILED"}},
        {"range": {"timestamp": {"gte": "now-1h"}}}
      ]
    }
  }
}
```

## Severity Levels

| Severity | Event Types | Aktion |
|----------|-------------|---------|
| **HIGH** | LOGIN_FAILED, UNAUTHORIZED_ACCESS, PRIVILEGE_ESCALATION_ATTEMPT, BRUTE_FORCE_DETECTED, INTEGRITY_VIOLATION | Sofortiges Logging + SIEM-Alert |
| **MEDIUM** | RATE_LIMIT_EXCEEDED, PERMISSION_DENIED, SUSPICIOUS_ACTIVITY | Standard-Logging + Monitoring |
| **LOW** | LOGIN_SUCCESS, DATA_READ, CONFIG_CHANGED | Standard-Logging |

HIGH-Severity-Events erzeugen zusätzlich `THEMIS_WARN` Log-Meldungen.

## Best Practices

### 1. Hash Chain aktivieren (Produktion)

```bash
export THEMIS_AUDIT_ENABLE_HASH_CHAIN=true
export THEMIS_AUDIT_CHAIN_STATE_FILE=/var/lib/themis/audit_chain.json
```

**Wichtig**: Chain State File auf Read-Only-Filesystem oder mit Backup schützen.

### 2. Regelmäßige Integritätsprüfung

```bash
# Cron-Job (täglich 3 Uhr)
0 3 * * * /usr/local/bin/themis-verify-audit-chain
```

```cpp
// themis-verify-audit-chain
#include "utils/audit_logger.h"

int main() {
    auto logger = themis::utils::AuditLogger::fromEnv();
    if (!logger->verifyChainIntegrity()) {
        // Incident Response: E-Mail, Pager, SIEM-Alert
        std::cerr << "CRITICAL: Audit log tampering detected!\n";
        return 1;
    }
    std::cout << "Audit log integrity verified\n";
    return 0;
}
```

### 3. SIEM-Integration einrichten

**Rsyslog-Konfiguration** (auf Audit-Server):
```conf
# /etc/rsyslog.d/50-themis.conf
:programname, isequal, "themis-audit" /var/log/themis/audit.log
& stop
```

**Splunk Inputs**:
```conf
[http://themis-audit]
token = your-hec-token
sourcetype = themis:audit
index = security
```

### 4. Rotation und Archivierung

```bash
# logrotate configuration
/var/log/themis/audit.jsonl {
    daily
    rotate 365
    compress
    delaycompress
    missingok
    notifempty
    postrotate
        # Nach Rotation: Chain State speichern
        /usr/local/bin/themis-save-chain-state
    endscript
}
```

### 5. Security Event Monitoring

Kritische Events überwachen:

```cpp
// Rate Limiter für LOGIN_FAILED
std::map<std::string, int> failed_logins;

void onLoginFailed(const std::string& user_id) {
    failed_logins[user_id]++;
    
    logger.logSecurityEvent(
        SecurityEventType::LOGIN_FAILED,
        user_id,
        "/api/login",
        {{"attempt", failed_logins[user_id]}}
    );
    
    if (failed_logins[user_id] >= 5) {
        logger.logSecurityEvent(
            SecurityEventType::BRUTE_FORCE_DETECTED,
            user_id,
            "/api/login",
            {{"total_attempts", failed_logins[user_id]}}
        );
        // Account sperren, IP blocken
    }
}
```

## Troubleshooting

### Hash Chain Fehler

**Problem**: `Chain integrity violation at entry 42`

**Ursachen**:
1. Manuelle Änderung der Log-Datei
2. Dateisystem-Korruption
3. Tatsächliche Manipulation

**Lösung**:
```bash
# 1. Backup erstellen
cp /var/log/themis/audit.jsonl /tmp/audit_backup.jsonl

# 2. Letzte bekannte gute Chain wiederherstellen
cp /var/lib/themis/audit_chain.json.bak /var/lib/themis/audit_chain.json

# 3. Integritätsprüfung
themis-verify-audit-chain

# 4. Forensische Analyse
diff /var/log/themis/audit.jsonl /tmp/audit_backup.jsonl
```

### SIEM Forwarding Fehler

**Problem**: `Failed to create syslog socket`

**Lösung**:
```bash
# 1. Firewall-Regel (UDP 514)
sudo ufw allow out 514/udp

# 2. Netzwerk-Konnektivität prüfen
nc -u -v 192.168.1.100 514

# 3. SELinux Policy (falls applicable)
sudo setsebool -P nis_enabled 1
```

**Problem**: Splunk Events kommen nicht an

**Hinweis**: Splunk HEC noch nicht implementiert (libcurl erforderlich).

### Chain State File Locks

**Problem**: `Failed to save chain state`

**Lösung**:
```bash
# 1. Berechtigungen prüfen
ls -la /var/lib/themis/audit_chain.json

# 2. Ownership korrigieren
sudo chown themis:themis /var/lib/themis/audit_chain.json

# 3. Verzeichnis-Berechtigungen
sudo chmod 750 /var/lib/themis
```

## Performance

### Overhead-Messung

- **Hash Chain**: ~0.5ms pro Entry (SHA256)
- **SIEM Forward**: ~1-2ms (UDP), ~10-20ms (HTTP)
- **Disk I/O**: ~0.1ms (append-only)

**Total**: ~1.5-3ms pro Security Event (vernachlässigbar bei typischer Last).

### Batch-Logging

Für hohe Frequenz (>1000 events/sec):

```cpp
// Event-Queue mit Batch-Write
std::queue<nlohmann::json> event_queue;
std::mutex queue_mu;

void batchWorker() {
    while (running) {
        std::vector<nlohmann::json> batch;
        {
            std::lock_guard<std::mutex> lock(queue_mu);
            while (!event_queue.empty() && batch.size() < 100) {
                batch.push_back(event_queue.front());
                event_queue.pop();
            }
        }
        
        for (auto& event : batch) {
            logger.logEvent(event);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}
```

## Compliance

### DSGVO/GDPR

- **PII_ACCESSED/REVEALED/ERASED** Events für Audit Trail
- 365 Tage Aufbewahrung (konfigurierbar)
- Hash Chain als Manipulationsschutz

### SOC 2

- Umfassende Security Event Coverage
- SIEM-Integration für Continuous Monitoring
- Tamper-Proof Logging (Hash Chain)

### HIPAA

- Audit-Trail für alle PHI-Zugriffe (DATA_READ/WRITE/DELETE)
- PII Events für personenbezogene Gesundheitsdaten
- Verschlüsselung + Hash Chain = Defense in Depth

## API Reference

### logSecurityEvent()

```cpp
void logSecurityEvent(
    SecurityEventType event_type,
    const std::string& user_id,
    const std::string& resource,
    const nlohmann::json& details = {}
);
```

**Parameter**:
- `event_type`: Typ des Security Events (siehe SecurityEventType enum)
- `user_id`: Benutzerkennung (E-Mail, Username, "system" für automatische Events)
- `resource`: Betroffene Ressource (API-Pfad, Datenbank-Tabelle, etc.)
- `details`: Zusätzliche Event-spezifische Informationen (optional)

**Beispiel**:
```cpp
logger.logSecurityEvent(
    SecurityEventType::KEY_ROTATED,
    "system",
    "master_key",
    {{"old_key_version", 1}, {"new_key_version", 2}}
);
```

### verifyChainIntegrity()

```cpp
bool verifyChainIntegrity();
```

**Rückgabe**: `true` bei intakter Chain, `false` bei Manipulation.

**Hinweis**: Liest komplette Log-Datei, kann bei großen Logs (>1GB) langsam sein.

### getChainState()

```cpp
nlohmann::json getChainState() const;
```

**Rückgabe**:
```json
{
  "last_hash": "e3b0c44...",
  "entry_count": 1234,
  "last_timestamp_ms": 1704067200000,
  "chain_enabled": true
}
```

## Migration

### Von Standard-Logging zu Hash Chain

1. **Aktuelles Log sichern**:
   ```bash
   cp /var/log/themis/audit.jsonl /var/log/themis/audit_pre_chain.jsonl
   ```

2. **Hash Chain aktivieren**:
   ```bash
   export THEMIS_AUDIT_ENABLE_HASH_CHAIN=true
   export THEMIS_AUDIT_CHAIN_STATE_FILE=/var/lib/themis/audit_chain.json
   ```

3. **Server neu starten**:
   ```bash
   sudo systemctl restart themis
   ```

4. **Chain initialisiert** (Genesis-Hash `000...000`).

**Hinweis**: Alte Einträge (ohne chain-Felder) werden bei Verification übersprungen.

## Weitere Informationen

- [Audit Logger Implementation](../src/utils/audit_logger.cpp)
- [Security Event Types](../include/utils/audit_logger.h)
- [Secrets Management](SECRETS_MANAGEMENT.md)
- [TLS Setup](TLS_SETUP.md)
