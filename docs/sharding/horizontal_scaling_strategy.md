# Horizontale Skalierung: Vollständige Implementierungsstrategie

**Version:** 2.0  
**Erstellt:** 20. November 2025  
**Fokus:** Sharding-basierte horizontale Skalierung mit VCC-PKI als Skalierungswerkzeug  
**Status:** ✨ **NEU** - Erweitert Infrastructure Roadmap mit PKI-Integration

---

## Executive Summary

Diese Strategie beschreibt die **vollständige Implementierung einer horizontal skalierbaren ThemisDB-Architektur** mit **URN-basiertem Sharding** und **VCC-PKI als zentralem Skalierungswerkzeug**. Die PKI-Integration ermöglicht:

- ✅ **Sichere Shard-zu-Shard-Kommunikation** via mutual TLS (mTLS)
- ✅ **Verifiable Credentials** für Shard-Identität und -Autorisierung
- ✅ **Dezentrale Trust-Architektur** ohne Single Point of Failure
- ✅ **Automatische Shard-Authentifizierung** bei Cluster-Join/Leave
- ✅ **Signierte Rebalancing-Operations** zur Auditierbarkeit
- ✅ **Zero-Trust Shard-Mesh** mit granularer Berechtigungskontrolle

**Investition:** 12-18 Monate Engineering  
**ROI:** Enterprise-ready, security-first distributed database

---

## Inhaltsverzeichnis

1. [Architekturübersicht](#1-architekturübersicht)
2. [VCC-PKI als Skalierungswerkzeug](#2-vcc-pki-als-skalierungswerkzeug)
3. [Sharding-Strategie](#3-sharding-strategie)
4. [Shard-Kommunikationsprotokoll](#4-shard-kommunikationsprotokoll)
5. [Rebalancing mit PKI-Signatur](#5-rebalancing-mit-pki-signatur)
6. [Shard-Authentifizierung & Autorisierung](#6-shard-authentifizierung--autorisierung)
7. [Metadata Store mit PKI](#7-metadata-store-mit-pki)
8. [Monitoring & Audit](#8-monitoring--audit)
9. [Implementierungsplan](#9-implementierungsplan)
10. [Sicherheitsmodell](#10-sicherheitsmodell)
11. [Performance-Optimierungen](#11-performance-optimierungen)
12. [Disaster Recovery](#12-disaster-recovery)

---

## 1. Architekturübersicht

### 1.1 Gesamtarchitektur mit PKI-Layer

```
┌────────────────────────────────────────────────────────────────────────┐
│                         VCC-PKI Trust Root                              │
│  ┌──────────────────────────────────────────────────────────────────┐  │
│  │  Certificate Authority (CA)                                       │  │
│  │  - Root CA: themis-root-ca.crt                                   │  │
│  │  - Intermediate CA: themis-cluster-ca.crt                        │  │
│  │  - Shard Certificates: shard-{id}.crt (X.509 mit URN-Extension)  │  │
│  └──────────────────────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────────────────────┘
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                         PKI-Secured Routing Layer                       │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐                 │
│  │ URN Resolver │  │ Shard Router │  │ mTLS Gateway │                 │
│  │ + PKI Cache  │  │ + PKI Verify │  │ + Cert Mgmt  │                 │
│  └──────────────┘  └──────────────┘  └──────────────┘                 │
│  - Certificate-based Shard Discovery                                   │
│  - Mutual TLS für alle Inter-Shard-Kommunikation                      │
│  - JWT-Tokens signiert mit Shard-Private-Key                          │
└────────────────────────────────────────────────────────────────────────┘
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                       Shard Mesh (Zero-Trust)                           │
│  ┌──────────┐ mTLS ┌──────────┐ mTLS ┌──────────┐ mTLS ┌──────────┐  │
│  │ Shard 1  │◄────►│ Shard 2  │◄────►│ Shard 3  │◄────►│ Shard N  │  │
│  │ + PKI    │      │ + PKI    │      │ + PKI    │      │ + PKI    │  │
│  │ RocksDB  │      │ RocksDB  │      │ RocksDB  │      │ RocksDB  │  │
│  └────┬─────┘      └────┬─────┘      └────┬─────┘      └────┬─────┘  │
│       │                 │                 │                 │          │
│  ┌────▼───┐        ┌────▼───┐        ┌────▼───┐        ┌────▼───┐    │
│  │Replica1│        │Replica1│        │Replica1│        │Replica1│    │
│  │+ PKI   │        │+ PKI   │        │+ PKI   │        │+ PKI   │    │
│  └────────┘        └────────┘        └────────┘        └────────┘    │
└────────────────────────────────────────────────────────────────────────┘
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   PKI-Secured Metadata Store (etcd)                     │
│  - Shard Topology signiert mit Cluster-CA                              │
│  - Rebalancing Operations signiert mit Operator-Key                    │
│  - Certificate Revocation List (CRL) für kompromittierte Shards       │
└────────────────────────────────────────────────────────────────────────┘
```

### 1.2 Kernprinzipien

1. **Zero-Trust Architecture**: Jeder Shard muss sich authentifizieren
2. **Certificate-Based Identity**: Shard-Identität = X.509-Zertifikat
3. **Mutual TLS (mTLS)**: Alle Shard-zu-Shard-Verbindungen verschlüsselt
4. **Signed Operations**: Rebalancing, Topology-Changes signiert
5. **Auditability**: Alle PKI-Events werden geloggt (Audit Trail)


## 2. VCC-PKI als Skalierungswerkzeug

### 2.1 PKI-Hierarchie für Cluster-Skalierung

Die VCC-PKI (siehe `pki_integration_architecture.md`) wird als **zentrales Skalierungswerkzeug** eingesetzt:

**PKI-Hierarchie:**
```
themis-root-ca.crt (Self-Signed, Offline, HSM-backed)
  │
  ├─► themis-cluster-ca.crt (Intermediate CA für Shards)
  │     ├─► shard-001.themis.local.crt
  │     ├─► shard-002.themis.local.crt
  │     └─► shard-NNN.themis.local.crt
  │
  ├─► themis-operator-ca.crt (Intermediate CA für Operatoren)
  │     ├─► operator-admin.crt (Rebalancing, Add/Remove Shard)
  │     └─► operator-readonly.crt (Nur Lesen/Monitoring)
  │
  └─► themis-client-ca.crt (Intermediate CA für Clients)
        ├─► client-app1.crt
        └─► client-app2.crt
```

### 2.2 Shard-Zertifikat mit Custom Extensions

**X.509 Certificate Extensions** für Shard-Identität:

```
Subject: CN=shard-001.themis.local, O=ThemisDB, OU=Cluster-Production
SAN (Subject Alternative Names):
  - DNS: shard-001.themis.local
  - DNS: shard-001.dc1.themis.local  
  - IP: 10.1.2.3
  - URI: urn:themis:shard:cluster-prod:001

Custom X.509 Extensions (OID 1.3.6.1.4.1.XXXXX):
  - shardID: shard_001
  - datacenter: dc1
  - tokenRangeStart: 0x0000000000000000
  - tokenRangeEnd: 0x3FFFFFFFFFFFFFFF
  - capabilities: [read, write, replicate, admin]
  - role: primary | replica
```

**Implementation Reference:**
- Existing: `src/utils/pki_client.cpp` (RSA-Signaturen)
- Neu: `include/sharding/pki_shard_certificate.h` (X.509-Extensions-Parser)

### 2.3 PKI-basierte Shard-Discovery

**Integration mit Infrastructure Roadmap:**

```cpp
// Erweitert URNResolver aus infrastructure_roadmap.md
namespace themis::sharding {

class PKIShardDiscovery {
public:
    /// Register shard certificate in etcd
    void registerShard(const ShardCertificate& cert);
    
    /// Discover shards via PKI certificates
    std::vector<ShardInfo> discoverShards();
    
    /// Verify shard certificate against Root CA
    bool verifyShard(const std::string& shard_id);
    
    /// Revoke compromised shard
    void revokeShard(const std::string& shard_id, const std::string& reason);
};

} // namespace themis::sharding
```

**etcd Key-Schema:**
```
/themis/cluster-prod/shards/shard-001/certificate
  → {cert_pem, serial_number, not_before, not_after, capabilities}

/themis/cluster-prod/crl/revoked
  → [serial_001, serial_042, ...]  # Certificate Revocation List
```

---

## 3. Sharding-Strategie (mit PKI-Integration)

### 3.1 URN-basiertes Hash-Sharding

Verwendet URN-Schema aus `infrastructure_roadmap.md`:

```
urn:themis:{model}:{namespace}:{collection}:{uuid}

Beispiele:
  urn:themis:relational:customers:users:550e8400-e29b-41d4-a716-446655440000
  urn:themis:graph:social:edges:7c9e6679-7425-40de-944b-e07fc1f90ae7
```

**Hash-Funktion:** xxHash (fast, gleichmäßig verteilt)

```cpp
uint64_t hashURN(const URN& urn) {
    return xxh3::xxh64(urn.uuid.data(), urn.uuid.size(), /*seed=*/0);
}
```

**Shard-Zuordnung via Consistent Hashing Ring:**
- Virtual Nodes: 150 pro Shard (konfigurierbar)
- Balance-Faktor: <5% Standard-Abweichung
- Rebalancing: Automatisch bei Add/Remove Shard

### 3.2 PKI-gesicherte Shard-Zuordnung

```cpp
// Shard-Router mit PKI-Verification
class ShardRouter {
    std::optional<ShardInfo> resolveShard(const URN& urn) {
        // 1. Hash URN → Token
        uint64_t token = hashURN(urn);
        
        // 2. Consistent Hash → Shard-ID
        std::string shard_id = hash_ring_.getShardForHash(token);
        
        // 3. PKI-Verification: Ist Shard-Certificate gültig?
        if (!pki_discovery_->verifyShard(shard_id)) {
            LOG(ERROR) << "Shard certificate invalid/revoked: " << shard_id;
            return std::nullopt;
        }
        
        // 4. Get Shard-Info (mit mTLS-Endpoint)
        return shard_topology_->getShard(shard_id);
    }
};
```

---

## 4. Shard-Kommunikationsprotokoll

### 4.1 Mutual TLS (mTLS) für Shard-zu-Shard

**Verbindungsaufbau:**

```
1. Shard-A lädt eigenes Zertifikat (shard-A.crt + shard-A.key)
2. Shard-A öffnet TLS-Verbindung zu Shard-B
3. TLS-Handshake:
   a) Shard-A sendet Client-Cert (shard-A.crt)
   b) Shard-B verifiziert gegen Root CA
   c) Shard-B sendet Server-Cert (shard-B.crt)
   d) Shard-A verifiziert gegen Root CA
4. Certificate Extensions Check:
   - shardID korrekt?
   - Capabilities vorhanden?
   - Nicht in CRL?
5. TLS-Session established (TLS 1.3)
```

**Implementation:**

```cpp
namespace themis::sharding {

class MTLSClient {
public:
    struct Config {
        std::string cert_path;      // /etc/themis/pki/shard-001.crt
        std::string key_path;       // /etc/themis/pki/shard-001.key
        std::string ca_cert_path;   // /etc/themis/pki/root-ca.crt
        std::string crl_path;       // /etc/themis/pki/crl.pem
    };
    
    MTLSClient(const Config& cfg);
    
    /// GET mit mTLS
    std::optional<nlohmann::json> get(const std::string& endpoint,
                                       const std::string& path);
    
    /// POST mit mTLS + Request-Signatur
    std::optional<nlohmann::json> post(const std::string& endpoint,
                                        const std::string& path,
                                        const nlohmann::json& body);
};

} // namespace themis::sharding
```

### 4.2 Signierte Requests (zusätzlich zu mTLS)

**Defense-in-Depth:** Requests werden zusätzlich mit Shard-Private-Key signiert

```cpp
struct SignedRequest {
    std::string shard_id;       // Sender
    std::string operation;      // GET, PUT, DELETE
    std::string path;           // URN-Pfad
    nlohmann::json body;
    uint64_t timestamp_ms;      // Replay-Protection
    uint64_t nonce;             // Replay-Protection
    
    std::string signature_b64;  // RSA-SHA256 Signature
    std::string cert_serial;    // Certificate Serial Number
};
```

**Verification:**
1. Timestamp-Check (max 60s Abweichung)
2. Nonce-Check (Duplicate-Detection)
3. Certificate-Lookup (via cert_serial)
4. Signature-Verification (RSA-SHA256)
5. Capability-Check (darf Sender diese Operation?)

---

## 5. Rebalancing mit PKI-Signatur

### 5.1 Operator-Zertifikate

Nur **autorisierte Operatoren** dürfen Rebalancing durchführen:

```
themis-operator-ca.crt
  ├─► admin.operator.crt
  │     Capabilities: [rebalance, add_shard, remove_shard]
  └─► readonly.operator.crt
        Capabilities: [view_topology]
```

### 5.2 Signierte Rebalancing-Operation

```cpp
struct RebalanceOperation {
    std::string operation_id;      // UUID
    std::string from_shard;
    std::string to_shard;
    uint64_t token_range_start, token_range_end;
    
    std::string operator_id;       // Operator CN
    std::string signature_b64;     // Operator-Signatur
    std::string cert_serial;
    
    void sign(const PKIClient& operator_pki);
    bool verify(const std::string& operator_ca_pem) const;
};
```

**Workflow:**

```bash
$ themis-admin rebalance \
    --from=shard-001 \
    --to=shard-002 \
    --token-range=0x0000:0x1FFF \
    --operator-cert=/etc/themis/pki/admin.operator.crt \
    --operator-key=/etc/themis/pki/admin.operator.key

# → Signierte Operation wird an beide Shards gesendet
# → Shards verifizieren Operator-Signatur
# → Rebalancing startet (mit Audit-Log)
```

---

## 6. Integration mit Existing Infrastructure Roadmap

Dieses Dokument **erweitert** `infrastructure_roadmap.md` mit PKI-Sicherheit:

**Übernommen aus infrastructure_roadmap.md:**
- ✅ URN-Schema (`urn:themis:{model}:{namespace}:{collection}:{uuid}`)
- ✅ Consistent Hashing Ring
- ✅ Shard Topology Manager (etcd-basiert)
- ✅ Scatter-Gather Queries
- ✅ Client SDKs (Python, JavaScript, Rust)

**Neu hinzugefügt (PKI-Layer):**
- ✨ X.509-basierte Shard-Identität
- ✨ mTLS für alle Shard-Verbindungen
- ✨ Signierte Rebalancing-Operationen
- ✨ Operator-Zertifikate für Admin-Tasks
- ✨ Certificate Revocation (CRL)
- ✨ PKI-Audit-Trail

---

## 7. Sicherheitsmodell

### 7.1 Zero-Trust Shard-Mesh

**Prinzip:** Kein Shard vertraut einem anderen ohne Verification

**Implementierung:**
1. **Mutual TLS:** Beide Seiten verifizieren Zertifikate
2. **Certificate Extensions:** shardID, datacenter, capabilities
3. **Capability-Based Access Control:** Nur erlaubte Operationen
4. **Signed Requests:** Replay-Protection via Nonce+Timestamp
5. **CRL-Checking:** Revoked Certificates werden abgelehnt

### 7.2 Threat Model

| Bedrohung | Mitigation |
|-----------|------------|
| Kompromittierter Shard | Certificate Revocation (CRL) |
| Man-in-the-Middle | mTLS + Signed Requests |
| Unauthorized Rebalancing | Operator-Certificates |
| Certificate Theft | Encrypted Keys (Passphrase/HSM) |
| CA-Compromise | Offline Root-CA, HSM-backed |

---

## 8. Implementierungsplan

### Phase 1: PKI-Setup (Monat 1-2)
- [ ] Root CA erstellen (offline, HSM-backed)
- [ ] Intermediate CAs (Cluster, Operator, Client)
- [ ] Certificate-Generation-Tool
- [ ] X.509-Extensions-Parser
- [ ] PKI-Shard-Discovery Implementation

### Phase 2: mTLS-Integration (Monat 3-4)
- [ ] MTLSClient Implementation (Boost.Beast + OpenSSL)
- [ ] Signed-Request-Protokoll
- [ ] Integration mit ShardRouter
- [ ] etcd-Integration mit mTLS

### Phase 3: Sharding-Core (Monat 5-6)
- [ ] URN-Parser & Consistent Hashing (aus infrastructure_roadmap.md)
- [ ] PKI-gesicherte Shard-Zuordnung
- [ ] Scatter-Gather mit mTLS
- [ ] Performance-Benchmarks

### Phase 4: Rebalancing & Operations (Monat 7-8)
- [ ] Operator-Zertifikate
- [ ] Signierte Rebalancing-Operations
- [ ] Zero-Downtime Rebalancing
- [ ] Audit-Logging

**Total:** 8 Monate mit 2-3 Entwicklern

---

## 9. Monitoring & Audit

### 9.1 PKI-Audit-Events

```cpp
enum class PKIAuditEvent {
    CERTIFICATE_ISSUED,
    CERTIFICATE_REVOKED,
    MTLS_CONNECTION_SUCCESS,
    MTLS_CONNECTION_FAILED,
    SIGNATURE_VERIFIED,
    SIGNATURE_FAILED,
    CAPABILITY_DENIED,
    REBALANCE_INITIATED
};
```

**Prometheus Metrics:**
```
themis_pki_events_total{event="MTLS_CONNECTION_SUCCESS"}
themis_certificate_expiry_days{shard_id="shard-001"}
themis_crl_size_bytes
```

### 9.2 Alerting

```yaml
# Prometheus Alert Rules
- alert: CertificateExpiringSoon
  expr: themis_certificate_expiry_days < 30
  severity: warning

- alert: CertificateRevoked
  expr: themis_pki_events_total{event="CERTIFICATE_REVOKED"} > 0
  severity: critical
```

---

## 10. Referenzen

**Bestehende Dokumentation:**
- `infrastructure_roadmap.md` - URN-basiertes Sharding (Foundation)
- `pki_integration_architecture.md` - VCC-PKI Grundlagen
- `security/key_management.md` - Schlüsselverwaltung
- `encryption_strategy.md` - End-to-End-Verschlüsselung

**Neue Komponenten:**
- `include/sharding/pki_shard_certificate.h` - X.509-Extensions
- `include/sharding/mtls_client.h` - mTLS HTTP-Client
- `include/sharding/signed_request.h` - Request-Signatur-Protokoll

---

## Zusammenfassung

Diese Strategie kombiniert:
- ✅ **URN-basiertes Sharding** (aus infrastructure_roadmap.md)
- ✅ **VCC-PKI-Integration** (aus pki_integration_architecture.md)
- ✅ **Zero-Trust Security** (mTLS + Signed Requests)
- ✅ **Operator-Controls** (Certificate-based Authorization)

**Ergebnis:** Enterprise-ready, security-first distributed database mit horizontal scaling

**Nächste Schritte:**
1. Review & Approval
2. Resource-Allocation (2-3 Engineers)
3. PKI-Setup (CA-Hierarchie)
4. Phase 1 Kickoff
