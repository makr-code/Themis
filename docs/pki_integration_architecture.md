# PKI Integration Architecture

**Version:** 1.0  
**Datum:** 09. November 2025  
**Status:** Design Document

---

## Überblick

ThemisDB benötigt eine **interne PKI-Komponente** (`PKIKeyProvider`), obwohl ein externer Python-basierter VCC-PKI Server existiert. Dieses Dokument erklärt die Architektur, Verantwortlichkeiten und Notwendigkeit beider Komponenten.

---

## Architektur: Zwei-Schichten-Modell

```
┌─────────────────────────────────────────────────────────────────┐
│                         ThemisDB (C++)                           │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │                    PKIKeyProvider                          │ │
│  │  - KEK Caching (In-Memory)                                │ │
│  │  - Zertifikat-Validierung (lokal)                         │ │
│  │  - Schlüssel-Ableitung (HKDF/PBKDF2)                      │ │
│  │  - Rotation-Management (Hot-Reload)                       │ │
│  │  - Fallback-Mechanismen (Offline-Modus)                   │ │
│  └────────────────┬───────────────────────────────────────────┘ │
│                   │                                              │
│                   │ REST API (HTTPS)                             │
│                   │ - GET /certificates/{id}                     │
│                   │ - POST /certificates/request                 │
│                   │ - GET /crl (Certificate Revocation List)     │
│                   ▼                                              │
└─────────────────────────────────────────────────────────────────┘
                    │
                    │ TLS/mTLS Connection
                    ▼
┌─────────────────────────────────────────────────────────────────┐
│               VCC-PKI Server (Python)                            │
│  - Certificate Authority (CA)                                    │
│  - Zertifikat-Ausstellung (X.509)                               │
│  - CRL-Management (Revocation)                                   │
│  - HSM-Integration (optional)                                    │
│  - Audit-Logging (Zertifikatsanforderungen)                     │
│  - Compliance (eIDAS, BSI-C5)                                    │
└─────────────────────────────────────────────────────────────────┘
```

---

## Warum brauchen wir PKIKeyProvider?

### Problem 1: **Latenz & Verfügbarkeit**

**Szenario:** ThemisDB verschlüsselt 10.000 Dokumente/Sekunde mit Field-Level Encryption.

**Ohne PKIKeyProvider:**
```cpp
// JEDE Verschlüsselung macht einen HTTP-Request
for (auto& doc : documents) {
    auto kek = httpClient.get("https://pki-server/certificates/kek_123"); // 50-100ms Latenz
    auto dek = deriveKey(kek, doc.field);
    doc.encrypt(dek);
}
// Throughput: ~10-20 docs/sec (HTTP-Overhead dominiert)
```

**Mit PKIKeyProvider:**
```cpp
// KEK wird EINMAL geholt, dann gecacht
auto kek = pkiProvider.getKEK("kek_123"); // Cached, <1ms

for (auto& doc : documents) {
    auto dek = deriveKey(kek, doc.field); // Lokal, <0.1ms
    doc.encrypt(dek);
}
// Throughput: 10.000+ docs/sec (Nur Crypto-Overhead)
```

**Performance-Gewinn:** 500-1000x

---

### Problem 2: **Offline-Betrieb**

**Szenario:** VCC-PKI Server ist kurzzeitig nicht erreichbar (Netzwerk-Partition, Wartung).

**Ohne PKIKeyProvider:**
- ❌ ThemisDB kann nicht schreiben (Encryption fehlschlägt)
- ❌ Kompletter Service-Ausfall
- ❌ Recovery dauert, bis PKI wieder online

**Mit PKIKeyProvider:**
- ✅ Gecachte KEKs bleiben gültig (TTL: 24h)
- ✅ Schreib-Operationen laufen weiter
- ✅ Nur Rotation blockiert (kann verzögert werden)
- ✅ Graceful Degradation statt Hard-Fail

**Availability-Verbesserung:** 99.9% → 99.99%

---

### Problem 3: **Zertifikat-Validierung**

**Szenario:** ThemisDB erhält ein Zertifikat vom PKI-Server. Ist es vertrauenswürdig?

**Ohne PKIKeyProvider:**
```cpp
// Naive Implementierung - UNSICHER
auto cert = httpClient.get("https://pki/cert/123");
useCertificate(cert); // Keine Validierung!
```

**Risiken:**
- 🔴 Man-in-the-Middle Attacken
- 🔴 Revozierte Zertifikate werden akzeptiert
- 🔴 Abgelaufene Zertifikate

**Mit PKIKeyProvider:**
```cpp
auto cert = pkiProvider.getCertificate("cert_123");
// Intern:
// 1. TLS-Verifikation (Server-Identität)
// 2. X.509-Chain-Validierung (bis Root-CA)
// 3. CRL-Check (Revocation List)
// 4. Expiry-Check (Not-Before/Not-After)
// 5. Key-Usage-Check (Encryption vs. Signing)
```

**Security-Verbesserung:** Verhindert kompromittierte Schlüssel

---

### Problem 4: **Schlüssel-Ableitung (Key Derivation)**

**Szenario:** Aus einem Master-KEK müssen 1.000 feldspezifische DEKs abgeleitet werden.

**Ohne PKIKeyProvider:**
```python
# Python PKI-Server müsste ALLE DEKs ableiten (CPU-intensiv)
POST /derive-keys
{
  "kek_id": "kek_123",
  "fields": ["name", "email", "ssn", ...] // 1.000 Felder
}
# Server-Last: Hoch, Response: Langsam
```

**Mit PKIKeyProvider:**
```cpp
// ThemisDB macht Ableitung lokal (parallelisiert)
auto kek = pkiProvider.getKEK("kek_123");

std::vector<DEK> deks;
#pragma omp parallel for
for (const auto& field : fields) {
    deks.push_back(deriveKey(kek, field)); // HKDF, lokal
}
// CPU-Last verteilt, <10ms für 1.000 Keys
```

**Skalierbarkeit:** O(1) Server-Last statt O(n)

---

### Problem 5: **Rotation ohne Downtime**

**Szenario:** KEK muss rotiert werden (monatlich, nach Compromise).

**Ohne PKIKeyProvider:**
```
1. PKI-Server generiert neuen KEK
2. ThemisDB muss ALLE verschlüsselten Daten neu verschlüsseln
3. Während Re-Encryption: Service blockiert
```

**Mit PKIKeyProvider:**
```cpp
// Dual-Key-Strategie
pkiProvider.rotateKEK("kek_123", "kek_124");

// Phase 1: Neue Daten mit kek_124 verschlüsseln
// Phase 2: Alte Daten lazy re-encrypten (Background Job)
// Phase 3: kek_123 nach 30 Tagen deaktivieren

// Service läuft durchgehend!
```

**Downtime:** 0 Sekunden

---

## Verantwortlichkeiten

### VCC-PKI Server (Python) - **Authoritative Source**

**Aufgaben:**
- ✅ **Certificate Authority:** X.509-Zertifikate ausstellen
- ✅ **Revocation:** CRL/OCSP für kompromittierte Zertifikate
- ✅ **Audit:** Alle Anfragen/Ausstellungen loggen
- ✅ **Compliance:** eIDAS-Konformität, BSI-C5-Anforderungen
- ✅ **HSM-Integration:** Sicherer Key-Storage (optional)

**NICHT zuständig für:**
- ❌ Caching (zu viele Clients, zu hohe Last)
- ❌ Key-Derivation (CPU-intensiv, nicht skalierbar)
- ❌ Offline-Fallbacks (Server darf ausfallen)

---

### PKIKeyProvider (C++ in ThemisDB) - **Client-seitige Abstraktion**

**Aufgaben:**
- ✅ **Caching:** KEKs im RAM (TTL: 24h, LRU-Eviction)
- ✅ **Validierung:** X.509-Chain, CRL, Expiry lokal prüfen
- ✅ **Key-Derivation:** HKDF/PBKDF2 für DEK-Ableitung
- ✅ **Rotation-Management:** Dual-Key-Window, Lazy Re-Encryption
- ✅ **Fallback:** Offline-Modus mit gecachten Keys
- ✅ **TLS-Client:** mTLS für sichere Kommunikation mit PKI-Server

**NICHT zuständig für:**
- ❌ Zertifikat-Ausstellung (nur Anfragen via REST)
- ❌ CA-Operationen (Signieren, Root-Key-Management)
- ❌ Compliance-Logging (macht PKI-Server)

---

## API-Interaktion

### Startup: KEK Bootstrap

```cpp
// ThemisDB startet
PKIKeyProvider provider("https://pki-server:8443", tlsConfig);

// 1. Lade aktuelle KEKs
auto keks = provider.bootstrap(); // GET /certificates?type=KEK
// Response: [{"id": "kek_123", "cert": "...", "valid_until": "2026-01-01"}]

// 2. Validiere Zertifikate
for (const auto& kek : keks) {
    if (!provider.validateCertificate(kek.cert)) {
        throw SecurityException("Invalid KEK certificate");
    }
}

// 3. Cache in-memory
provider.cacheKEKs(keks); // TTL: 24h

// ThemisDB ready to encrypt
```

---

### Runtime: Encryption Flow

```cpp
// Dokument verschlüsseln
void encryptDocument(BaseEntity& doc) {
    // 1. Hole KEK (gecacht, <1ms)
    auto kek = pkiProvider.getKEK("kek_123");
    
    // 2. Leite feldspezifischen DEK ab (lokal, <0.1ms)
    auto dek = deriveFieldKey(kek, doc.collection, doc.field);
    
    // 3. Verschlüssele (AES-256-GCM, ~1ms)
    auto ciphertext = encrypt(doc.value, dek);
    
    // 4. Speichere
    doc.setValue(ciphertext);
}

// Kein PKI-Server-Request! (außer bei Cache-Miss)
```

---

### Rotation: Zero-Downtime KEK-Wechsel

```cpp
// PKI-Admin triggert Rotation (extern)
// POST https://pki-server/certificates/kek_123/rotate

// ThemisDB wird benachrichtigt (Webhook oder Polling)
pkiProvider.onKEKRotation("kek_123", "kek_124");

// Intern:
// 1. Hole neuen KEK von PKI-Server
auto newKEK = httpClient.get("/certificates/kek_124");

// 2. Validiere
if (!validateCertificate(newKEK)) throw ...;

// 3. Dual-Key-Window aktivieren
cacheKEK(newKEK); // kek_123 UND kek_124 sind jetzt gültig

// 4. Neue Writes nutzen kek_124
currentKEK = "kek_124";

// 5. Background-Job: Re-Encrypt alte Daten
reEncryptionJob.start("kek_123" -> "kek_124");

// 6. Nach 30 Tagen: kek_123 löschen
scheduleEviction("kek_123", 30_days);
```

---

## Sicherheitsaspekte

### TLS/mTLS zur PKI-Kommunikation

```cpp
TLSConfig tlsConfig;
tlsConfig.serverCertPath = "/etc/themis/pki-server-cert.pem";
tlsConfig.clientCertPath = "/etc/themis/themis-client-cert.pem"; // mTLS
tlsConfig.clientKeyPath  = "/etc/themis/themis-client-key.pem";
tlsConfig.caCertPath     = "/etc/themis/ca-root.pem";

PKIKeyProvider provider("https://pki-server:8443", tlsConfig);

// Alle Requests nutzen mTLS:
// - PKI-Server identifiziert ThemisDB via Client-Cert
// - ThemisDB vertraut nur validen Server-Certs (Root-CA)
```

---

### Cache-Security

```cpp
// KEKs werden NUR im RAM gehalten (nie auf Disk)
class PKIKeyProvider {
private:
    std::unordered_map<std::string, KEK> kek_cache_; // RAM
    std::mutex cache_mutex_;
    
    // Bei Shutdown: Explizit löschen
    ~PKIKeyProvider() {
        for (auto& [id, kek] : kek_cache_) {
            memset(&kek, 0, sizeof(KEK)); // Secure erase
        }
    }
};
```

**Warum wichtig?**
- Disk-basierte Caches könnten bei Compromise extrahiert werden
- RAM-Cache ist flüchtig (nach Restart weg)
- Verhindert Long-Term Key-Exposure

---

### CRL-Checking (Certificate Revocation List)

```cpp
bool PKIKeyProvider::validateCertificate(const X509Cert& cert) {
    // 1. Check Expiry
    if (cert.notAfter < now()) return false;
    
    // 2. Check CRL (gecacht, refresh alle 6h)
    auto crl = getCRL(); // GET /crl vom PKI-Server
    if (crl.contains(cert.serialNumber)) {
        log.error("Certificate {} is revoked!", cert.serialNumber);
        return false;
    }
    
    // 3. Verify Chain
    if (!verifyCertChain(cert, rootCA_)) return false;
    
    return true;
}
```

---

## Implementierungs-Roadmap

### Phase 1: Basis-Infrastruktur (2 Wochen)

**Komponenten:**
1. `VCCPKIClient` (REST-Client für PKI-Server)
   - GET/POST Endpoints
   - TLS/mTLS-Konfiguration
   - Timeout/Retry-Logik

2. `PKIKeyProvider` (KEK-Manager)
   - In-Memory-Cache (LRU, TTL)
   - Bootstrap-Logik
   - Certificate-Validierung (X.509, CRL)

**Tests:**
- Unit-Tests mit Mock-PKI-Server
- Integration-Tests mit lokalem Python PKI-Server
- Performance-Tests (Cache-Hit-Rate, Latenz)

---

### Phase 2: Key-Derivation (1 Woche)

**Komponenten:**
3. `deriveFieldKey(KEK, collection, field)` (HKDF)
4. Integration in `FieldEncryption`

**Tests:**
- HKDF-Vektoren (RFC 5869 Test-Cases)
- Multi-Threaded Key-Derivation (10.000 concurrent)
- Key-Stability (gleicher Input → gleicher Output)

---

### Phase 3: Rotation (1 Woche)

**Komponenten:**
5. Dual-Key-Window-Logic
6. Background Re-Encryption
7. Webhook/Polling für Rotation-Trigger

**Tests:**
- Zero-Downtime Rotation-Test
- Rollback-Szenarien
- Concurrent Read/Write während Rotation

---

### Phase 4: Production-Hardening (1 Woche)

**Komponenten:**
8. Monitoring (Cache-Hit-Rate, PKI-Latenz, Rotation-Status)
9. Alerting (Cert-Expiry, CRL-Fehler, PKI-Downtime)
10. Runbooks (Rotation-Manual, Incident-Response)

**Tests:**
- Chaos-Engineering (PKI-Server-Kill, Network-Partition)
- Load-Tests (100k Encryptions/sec)
- Security-Audit (Penetration-Test)

---

## Kosten-Nutzen-Analyse

### Entwicklungsaufwand

| Komponente | Aufwand | Risiko |
|------------|---------|--------|
| VCCPKIClient | 3 Tage | Niedrig (Standard HTTP-Client) |
| PKIKeyProvider Cache | 4 Tage | Mittel (Concurrency-Bugs) |
| Certificate-Validierung | 3 Tage | Hoch (Security-Critical) |
| Key-Derivation | 2 Tage | Niedrig (Standard HKDF) |
| Rotation-Logik | 4 Tage | Hoch (Race-Conditions) |
| Tests/Doku | 4 Tage | - |
| **TOTAL** | **20 Tage** (~4 Wochen) | - |

---

### Performance-Gewinn

| Metrik | Ohne PKIKeyProvider | Mit PKIKeyProvider | Verbesserung |
|--------|---------------------|-------------------|--------------|
| **Encryption Throughput** | 10-20 docs/sec | 10.000+ docs/sec | **500-1000x** |
| **PKI-Request-Latenz** | 50-100ms | <1ms (gecacht) | **50-100x** |
| **Availability** | 99.9% (PKI SPOF) | 99.99% (Offline-Fallback) | **+0.09%** |
| **Rotation-Downtime** | 1-2 Stunden | 0 Sekunden | **∞x** |

---

### Security-Gewinn

- ✅ **Revocation:** Kompromittierte Zertifikate werden erkannt (CRL-Check)
- ✅ **Expiry:** Abgelaufene KEKs werden nicht verwendet
- ✅ **mTLS:** Mutual Authentication zwischen ThemisDB ↔ PKI-Server
- ✅ **RAM-Only-Cache:** Keine persistenten KEKs auf Disk
- ✅ **Audit-Trail:** Alle PKI-Requests geloggt (Compliance)

---

## Alternativen (und warum sie nicht funktionieren)

### Alternative 1: "Direkt immer PKI-Server fragen"

**Problem:**
- 🔴 Latenz zu hoch (50-100ms per Request)
- 🔴 PKI-Server wird zum Bottleneck (10k QPS unmöglich)
- 🔴 Single-Point-of-Failure (PKI down → ThemisDB down)

**Fazit:** Nicht produktionstauglich

---

### Alternative 2: "KEKs in config.json speichern"

**Problem:**
- 🔴 Rotation erfordert Neustart
- 🔴 Keine Revocation (kompromittierte Keys bleiben gültig)
- 🔴 Compliance-Verstoß (Keys auf Disk)

**Fazit:** Sicherheitsrisiko

---

### Alternative 3: "Jeder DB-Node ist eigene CA"

**Problem:**
- 🔴 Keine zentrale Kontrolle (Chaos bei 100+ Nodes)
- 🔴 Cross-Node-Encryption unmöglich (verschiedene CAs)
- 🔴 Audit-Trail fragmentiert

**Fazit:** Nicht skalierbar

---

## Fazit

**PKIKeyProvider ist essentiell**, weil:

1. **Performance:** 500-1000x schnellere Verschlüsselung durch Caching
2. **Availability:** 99.99% Uptime durch Offline-Fallbacks
3. **Security:** CRL-Checks, Expiry-Validation, mTLS
4. **Skalierbarkeit:** O(1) Server-Last statt O(n)
5. **Zero-Downtime-Rotation:** Produktions-Ready

**VCC-PKI Server bleibt Authoritative Source**, aber PKIKeyProvider ist die notwendige **Client-seitige Abstraktionsschicht** für hohe Performance und Verfügbarkeit.

---

**Nächste Schritte (Aktualisiert 09. Nov 2025):**
1. ✅ Design-Review (dieses Dokument)
2. ✅ Implementierung VCCPKIClient (Tests: 6/6 PASS)
3. ✅ Implementierung PKIKeyProvider (GroupDEK Tests: 10/10 PASS)
4. ✅ JWTValidator Integration (Tests: 6/6 PASS)
5. ⏳ End-to-End Load/Failure Injection Tests (PKI-Ausfall Szenarien)
6. ⏳ Production-Deployment Checkliste (Monitoring, Dashboards, Alerts)

**Status:** Kernkomponenten implementiert & getestet; verbleibend: resilienz-orientierte Tests + Betriebsartefakte.

**Geänderte Timeline:** Feature-Complete; Production-Ready nach Abschluss der Resilienz-/Monitoring-Aufgaben (ETA 1 Woche).
