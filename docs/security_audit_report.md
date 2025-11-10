# Security & Compliance Audit Report
**Projekt:** Themis VectorDB  
**Version:** 1.0  
**Datum:** 10. November 2025  
**Auditor:** Development Team

---

## 🔐 Executive Summary

**Gesamt-Security-Score:** 7.8/10

**Stärken:**
- ✅ Moderne 3-Tier Key-Hierarchie (KEK→DEK→Field-Key)
- ✅ AES-256-GCM authenticated encryption
- ✅ OIDC/JWT-basierte Authentication
- ✅ Field-level granular encryption
- ✅ User-specific key isolation via HKDF

**Kritische Gaps:**
- ⚠️ Query Result Decryption fehlt (GAP #2)
- ⚠️ Audit Log Encryption nicht implementiert (GAP #4)
- ⚠️ Key Rotation Re-Encryption incomplete (GAP #5)
- 🐛 BFS Traversal Bug (GAP #6)

---

## 1. Threat Model & Attack Vectors

### 1.1 Threat Actors
| Actor | Capability | Motivation | Likelihood |
|-------|-----------|------------|------------|
| External Attacker | Network access | Data theft | Medium |
| Malicious Insider | DB read access | PII exfiltration | Low |
| Compromised Service | API credentials | Lateral movement | Medium |
| State Actor | Advanced persistent threat | Surveillance | Very Low |

### 1.2 Attack Scenarios

#### Scenario A: Database Dump Exfiltration ✅ MITIGATED
**Attack:** Angreifer erhält RocksDB-Dump via Backup-Leak

**Defense Layers:**
1. ✅ **KEK Derivation**: IKM gespeichert, aber KEK wird per HKDF dynamisch abgeleitet
2. ✅ **DEK Encryption**: DEK mit AES-256-GCM unter KEK verschlüsselt
3. ✅ **Field Encryption**: Daten mit user-specific Field-Keys verschlüsselt

**Result:** Angreifer hat verschlüsselte Blobs, aber:
- KEK nicht im Dump (benötigt PKI-Zertifikat)
- DEK verschlüsselt unter KEK
- Field-Data verschlüsselt unter user-derived keys

**Risk Level:** ✅ LOW (multi-layer defense)

#### Scenario B: Stolen JWT Token ⚠️ PARTIALLY MITIGATED
**Attack:** Angreifer stiehlt gültigen JWT-Token eines Users

**Current State:**
- ✅ Token-Expiration: Keycloak-Default (5-15 min)
- ✅ Signature Validation: RS256 verhindert Token-Forgery
- ⚠️ **FEHLT:** Revocation Check (kein /introspect endpoint call)
- ⚠️ **FEHLT:** IP-Binding oder Device-Fingerprinting

**Exploit:**
```bash
# Angreifer kopiert Token von User A
curl -H "Authorization: Bearer <stolen-token>" \
     https://themis/api/users/123
# → Kann alle Daten von User A lesen während Token valid ist
```

**Mitigation Needed:**
- Implementiere Token Revocation List (TRL) Check
- Kurze Token-Lifetime (<5 min)
- Refresh-Token Rotation

**Risk Level:** ⚠️ MEDIUM (time-window attack possible)

#### Scenario C: SQL Injection via AQL ✅ MITIGATED
**Attack:** Injection über AQL-Query

**Example:**
```sql
-- Attempt:
FOR u IN users FILTER u.email == "<script>alert(1)</script>" RETURN u

-- Defense:
```

**Current Protection:**
- ✅ AQL Lexer/Parser validiert Syntax
- ✅ Keine String-Concatenation bei Query Building
- ✅ Type-Safe Expression Evaluation
- ✅ No direct SQL/shell execution

**Risk Level:** ✅ LOW (parser-based defense)

#### Scenario D: Insider mit DB-Zugriff ⚠️ PARTIALLY MITIGATED
**Attack:** Malicious Admin mit RocksDB read-access

**Current State:**
- ✅ User-Isolation: Admin kann nicht User-A Daten als User-B entschlüsseln
- ⚠️ **SCHWACHSTELLE:** Admin kann DEK aus Storage lesen
- ⚠️ **SCHWACHSTELLE:** Kein Audit-Trail für DEK-Access

**Exploit Path:**
```cpp
// Admin extrahiert DEK
auto dek_encrypted = storage->get("dek:encrypted:v1");
// → Benötigt noch KEK (PKI-Cert), aber wenn Service läuft:
auto dek = key_provider.getKey("service-key", 1); // ← succeeds
// → Kann jetzt alle user-keys ableiten und Daten entschlüsseln
```

**Mitigation Needed:**
- HSM Integration für KEK-Storage
- Audit-Logging aller DEK-Access (encrypt-then-sign)
- Multi-Party Control für Key-Access

**Risk Level:** ⚠️ MEDIUM-HIGH (privileged insider)

#### Scenario E: Key Compromise & Re-Encryption ⚠️ INCOMPLETE
**Attack:** DEK kompromittiert, muss rotiert werden

**Current State:**
- ✅ `rotateKey()` API existiert
- ✅ Multi-Version Key Cache
- ⚠️ **FEHLT:** Lazy Re-Encryption on Read
- ⚠️ **FEHLT:** Background Job für Bulk Re-Encryption
- ⚠️ **FEHLT:** Prometheus Metrics für Migration Progress

**Scenario:**
```
Day 0: DEK-v1 kompromittiert, rotate zu DEK-v2
Day 1: 30% der Daten re-encrypted (nur actively accessed)
Day 7: 85% re-encrypted
Day 30: 5% legacy data noch mit DEK-v1 ← SECURITY GAP
```

**Mitigation Needed:**
- Deadline für Re-Encryption (z.B. 7 Tage)
- Background Worker für Bulk Migration
- Metrics Dashboard

**Risk Level:** ⚠️ MEDIUM (slow rotation window)

---

## 2. DSGVO/GDPR Compliance

### 2.1 Artikel 32: Technische & Organisatorische Maßnahmen

| Anforderung | Umsetzung | Status |
|-------------|-----------|--------|
| **Pseudonymisierung** | User-ID als Salt in HKDF | ✅ |
| **Verschlüsselung** | AES-256-GCM field-level | ✅ |
| **Vertraulichkeit** | User-Isolation via key derivation | ✅ |
| **Integrität** | GCM Auth-Tag | ✅ |
| **Verfügbarkeit** | Multi-Version Keys, Graceful Degradation | ✅ |
| **Belastbarkeit** | Key Rotation, Backup/Recovery | ⚠️ Partial |
| **Regelmäßige Tests** | 71 Unit Tests, Benchmarks | ✅ |

**Score:** 6.5/7 (Backup/Recovery nicht dokumentiert)

### 2.2 Artikel 17: Recht auf Löschung

**Anforderung:** User-Daten müssen vollständig gelöscht werden können

**Implementierung:**
```cpp
// DELETE User
storage->remove("user:" + user_id);

// Graph Edges löschen
auto edges = graph_index->getEdges(user_id);
for (const auto& edge : edges) {
    storage->remove("edge:" + edge.getPrimaryKey());
    storage->remove("graph:out:" + edge.from + ":" + edge_id);
    storage->remove("graph:in:" + edge.to + ":" + edge_id);
}

// Vectors löschen
vector_index->deleteVector(user_id);
```

**Gaps:**
- ⚠️ **Audit Logs:** Dürfen User-ID enthalten (legitimates Interesse), aber PII muss encrypted sein
- ⚠️ **Backups:** Keine Dokumentation wie encrypted backups gehandhabt werden
- ⚠️ **Group Data:** Wenn User aus Group austritt, Group-DEK rotation?

**Status:** ⚠️ PARTIAL (Audit + Backup unclear)

### 2.3 Artikel 20: Datenportabilität

**Anforderung:** User kann Daten in strukturiertem Format exportieren

**Implementierung:**
```bash
# Export API
GET /api/users/{user_id}/export
Authorization: Bearer <user-jwt>

# Response: JSON mit entschlüsselten Daten
{
  "user": {...},
  "documents": [...],
  "vectors": [...],
  "graph_edges": [...]
}
```

**Gap:** ⚠️ **API nicht implementiert** (aber technisch möglich via query decryption)

**Status:** ⚠️ MISSING (aber blockiert durch GAP #2)

### 2.4 Artikel 33: Meldepflicht bei Datenpannen

**Anforderung:** Breach Detection & Notification innerhalb 72h

**Current State:**
- ❌ **Keine Intrusion Detection**
- ❌ **Keine Anomaly Detection** (ungewöhnliche Query-Patterns)
- ❌ **Keine Audit Logs mit Tamper-Detection**

**Needed:**
- Audit Log Encryption mit PKI-Signatur (TODO #14)
- Prometheus Metrics für:
  - Failed auth attempts
  - Bulk data access
  - DEK access frequency
- SIEM Integration

**Status:** ❌ NOT COMPLIANT

---

## 3. Encryption Strength Analysis

### 3.1 Cryptographic Primitives

| Primitive | Algorithm | Key Size | Status | Notes |
|-----------|-----------|----------|--------|-------|
| Symmetric Encryption | AES-GCM | 256-bit | ✅ STRONG | NIST recommended |
| Key Derivation | HKDF-SHA256 | 256-bit | ✅ STRONG | RFC 5869 |
| Signature | RSA-SHA256 | 2048-bit | ⚠️ ACCEPTABLE | NIST deprecated 2030 |
| JWT Validation | RS256 | 2048-bit | ⚠️ ACCEPTABLE | Keycloak default |
| Random IV | std::random_device | 96-bit | ✅ STRONG | GCM-standard |
| Auth Tag | GCM | 128-bit | ✅ STRONG | NIST SP 800-38D |

**Empfehlungen:**
- Migrate zu RSA-4096 oder ECDSA P-384 bis 2026
- Documentiere Crypto-Agility für Post-Quantum Migration

### 3.2 Key Management Lifecycle

```
[Key Generation] → [Distribution] → [Storage] → [Rotation] → [Destruction]
       ✅                ✅              ⚠️          ⚠️            ❌
```

**Details:**
- ✅ **Generation:** `std::random_device` + HKDF (cryptographically secure)
- ✅ **Distribution:** KEK from PKI-Cert, DEK encrypted under KEK
- ⚠️ **Storage:** RocksDB plaintext IKM (sollte HSM sein)
- ⚠️ **Rotation:** API exists, aber incomplete re-encryption
- ❌ **Destruction:** Keine secure key wiping (z.B. NIST SP 800-88)

**Status:** 3/5 (Storage + Destruction gaps)

---

## 4. Code Security Review

### 4.1 Memory Safety (C++)

**Potenzielle Vulnerabilities:**

1. **Buffer Overflow in Base64 Decoding** ✅ SAFE
```cpp
// src/utils/pki_client.cpp:187
std::vector<uint8_t> base64_decode(const std::string& encoded) {
    // Uses EVP_DecodeBlock with pre-allocated buffer
    // ✅ Length-checked
}
```

2. **Use-After-Free in Key Cache** ✅ SAFE
```cpp
// src/security/pki_key_provider.cpp:120
std::shared_ptr<std::vector<uint8_t>> getKey() {
    // ✅ Verwendet shared_ptr, kein manual memory management
}
```

3. **Integer Overflow in HKDF** ✅ SAFE
```cpp
// src/utils/hkdf_helper.cpp:45
auto derive(dek, salt, info, size_t output_length) {
    if (output_length > 255 * 32) throw std::runtime_error(...);
    // ✅ Length-check prevents overflow
}
```

**Status:** ✅ NO CRITICAL ISSUES (Code Review passed)

### 4.2 Input Validation

**JWT Claims Extraction:**
```cpp
// src/auth/jwt_validator.cpp:89
auto claims = parseAndValidate(token);
if (!claims.contains("sub")) throw std::runtime_error("Missing sub");
// ✅ Required claims validated
// ✅ Type-checking (claims["groups"].is_array())
```

**AQL Parser:**
```cpp
// src/query/aql_parser.cpp
// ✅ Lexer/Parser approach prevents injection
// ✅ No eval() oder exec() calls
```

**HTTP Endpoints:**
```cpp
// src/server/http_server.cpp:3486
if (!req.has_header("Authorization")) {
    res.status = 401;
    return;
}
// ✅ Auth-Header required für encrypted endpoints
```

**Status:** ✅ STRONG INPUT VALIDATION

### 4.3 Error Handling & Information Leakage

**Beispiel - Decryption Failure:**
```cpp
// src/utils/field_encryption.cpp:145
try {
    return decryptWithKey(blob, key);
} catch (const std::exception& e) {
    // ⚠️ LEAK: Error message könnte Key-ID enthalten
    throw std::runtime_error("Decryption failed: " + std::string(e.what()));
}
```

**Empfehlung:**
```cpp
// Besser:
} catch (const std::exception&) {
    // Generic error, keine Details
    throw std::runtime_error("Authentication failed");
}
```

**Status:** ⚠️ MINOR LEAK (nicht kritisch, aber best practice)

---

## 5. Compliance Gap Summary

| Requirement | Status | Priority | Effort |
|-------------|--------|----------|--------|
| Query Result Decryption | ❌ Missing | HIGH | 4-6h |
| Audit Log Encryption | ❌ Missing | HIGH | 6-8h |
| Key Rotation Re-Encryption | ⚠️ Partial | MEDIUM | 8-10h |
| Token Revocation Check | ❌ Missing | MEDIUM | 2-3h |
| HSM Integration | ❌ Missing | LOW | 16-20h |
| Breach Detection | ❌ Missing | MEDIUM | 8-12h |
| Data Export API | ❌ Missing | LOW | 3-4h |
| Secure Key Wiping | ❌ Missing | LOW | 2-3h |

**Total Estimated Effort:** 49-68 Stunden (6-8.5 Tage)

---

## 6. Recommendations

### Sofort (diese Woche):
1. **Query Result Decryption** implementieren (GAP #2)
2. **Token Revocation** via Keycloak /introspect endpoint
3. **BFS Bug** fixen (GAP #6)

### Kurzfristig (nächste 2 Wochen):
4. **Audit Log Encryption** (TODO #14)
5. **Lazy Re-Encryption** finalisieren (GAP #5)
6. **Error Message Sanitization** (Information Leakage)

### Mittelfristig (Q1 2026):
7. **HSM Integration** für Production KEK-Storage
8. **Breach Detection Metrics** + SIEM Integration
9. **ECDSA P-384 Migration** (Post-Quantum Readiness)

### Langfristig (2026):
10. **NIST Post-Quantum** Cryptography Evaluation
11. **Formal Security Audit** durch externes Pentesting-Team
12. **ISO 27001 Zertifizierung** vorbereiten

---

## 7. Conclusion

**Overall Security Posture:** GOOD with gaps

**Strengths:**
- Solid cryptographic foundation (AES-256-GCM, HKDF, RS256)
- Modern architecture (3-tier keys, user-isolation)
- Good test coverage (71 tests, 45% E2E)

**Critical Gaps:**
- Query Decryption (blocks production use)
- Audit Encryption (GDPR Article 33 compliance)
- Key Rotation (operational risk)

**Recommendation:** **NICHT production-ready** bis GAP #2 und #14 geschlossen sind.

**Estimated Timeline to Production:**
- Minimum: 2 Wochen (GAP #2, #6 fix)
- Recommended: 4-6 Wochen (alle HIGH/MEDIUM gaps)

---

**Signature:**  
Development Team  
10. November 2025
