# Multi-Party-Verschlüsselung für ThemisDB

## Problem Statement

**Anforderung:** Mehrere Personen/Rollen/Gruppen/Abteilungen/Behörden müssen dieselben verschlüsselten Daten lesen und schreiben können, ohne dass:
- Jeder Nutzer einen eigenen verschlüsselten Datensatz erhält (Storage-Explosion)
- Daten bei Gruppen-Änderung re-encrypted werden müssen (Performance)
- Ein zentraler Admin alle Daten entschlüsseln kann (Zero-Trust verletzt)

**Beispiel-Szenarien:**
1. **HR-Abteilung**: Alle Mitarbeiter von `hr_team` können Gehalts-Daten lesen/schreiben
2. **Projekt-Team**: Alle Mitglieder von Projekt "Apollo" können Projektdokumente bearbeiten
3. **Behörden-übergreifend**: Polizei, Staatsanwaltschaft, Gericht können Ermittlungsakte teilen
4. **Rollenbasiert**: Alle mit Rolle `doctor` können Patientenakten lesen

---

## Lösung 1: Gruppenbasierte Schlüssel (Group-KEK)

### Architektur

```
┌────────────────────────────────────────────────┐
│  VCC-PKI Root CA                               │
│  └── Service Cert (themis-db.vcc.local)        │
└─────────────────┬──────────────────────────────┘
                  │ verschlüsselt
                  ▼
┌────────────────────────────────────────────────┐
│  Master KEK (Key Encryption Key)               │
│  - Pro ThemisDB-Instanz                        │
│  - Verschlüsselt mit PKI-Zertifikat            │
└─────────────────┬──────────────────────────────┘
                  │ verschlüsselt
                  ▼
┌────────────────────────────────────────────────┐
│  Gruppen-DEKs (Data Encryption Keys)           │
│  - Pro Gruppe/Rolle/Abteilung (z.B. "hr_team") │
│  - Gespeichert als: "key:group:hr_team"        │
│  - Verschlüsselt mit Master KEK                │
└─────────────────┬──────────────────────────────┘
                  │ verschlüsselt
                  ▼
         [Shared Encrypted Data]
```

### Implementierung

#### 1. Gruppen-DEK-Management

```cpp
// include/security/group_key_manager.h
class GroupKeyManager {
public:
    // Erstelle oder lade DEK für eine Gruppe
    std::vector<uint8_t> getGroupDEK(const std::string& group_name);
    
    // Rotiere Gruppen-Key (bei Mitglieder-Austritt)
    void rotateGroupKey(const std::string& group_name);
    
    // Prüfe ob User Zugriff auf Gruppe hat (via JWT)
    bool hasGroupAccess(const JWTClaims& claims, const std::string& group_name);
    
private:
    std::vector<uint8_t> master_kek_;  // Von PKI abgeleitet
    std::unordered_map<std::string, std::vector<uint8_t>> group_deks_; // Cache
};
```

#### 2. Verschlüsselung mit Gruppenkontext

```cpp
// Bei jedem Schreibvorgang
std::string jwt_token = request.get_header("Authorization");
auto claims = jwt_validator_->parseAndValidate(jwt_token);

// Bestimme zuständige Gruppe aus JWT oder Schema
std::string group = determineDataOwnerGroup(claims, field_schema);
// z.B. "hr_team" aus claims.groups[0] oder Schema-Annotation

// Hole Gruppen-DEK (gecacht, verschlüsselt in DB)
auto group_dek = group_key_mgr_->getGroupDEK(group);

// Leite Field-Key ab
auto field_key = HKDF_SHA256(
    group_dek,
    salt = field_name,
    info = "field-encryption:" + group
);

// Verschlüsseln
auto ciphertext = AES_GCM_encrypt(plaintext, field_key, nonce);

// Metadaten speichern (für Entschlüsselung)
entity.setField("salary_encrypted", base64(ciphertext));
entity.setField("salary_group", group);  // "hr_team"
```

#### 3. Entschlüsselung mit Access-Check

```cpp
// Bei jedem Lesevorgang
auto encrypted_salary = entity.getField("salary_encrypted");
auto data_group = entity.getField("salary_group").as<std::string>();

// Prüfe ob aktueller User Zugriff hat
if (!group_key_mgr_->hasGroupAccess(claims, data_group)) {
    throw UnauthorizedException("User not member of group: " + data_group);
}

// Hole Gruppen-DEK
auto group_dek = group_key_mgr_->getGroupDEK(data_group);

// Leite Field-Key ab (gleich wie beim Verschlüsseln)
auto field_key = HKDF_SHA256(group_dek, salt = "salary", info = "field-encryption:" + data_group);

// Entschlüsseln
auto plaintext = AES_GCM_decrypt(base64_decode(encrypted_salary), field_key, nonce);
```

### Vorteile
✅ **Shared Access**: Alle Gruppenmitglieder nutzen denselben DEK  
✅ **Keine Daten-Duplizierung**: Ein verschlüsselter Datensatz für alle  
✅ **Skalierbar**: Gruppenmitgliedschaft über JWT-Claims (Keycloak)  
✅ **Zero-Trust**: Admin ohne Gruppenmitgliedschaft kann nicht entschlüsseln  

### Nachteile
⚠️ **Key-Rotation komplex**: Bei Mitglieder-Austritt muss Gruppen-DEK rotiert werden  
⚠️ **Re-Encryption nötig**: Nach Key-Rotation müssen alle Daten re-encrypted werden  
⚠️ **Granularität**: Nur auf Gruppen-Ebene, nicht pro User  

---

## Lösung 2: Envelope-Encryption (Multi-Recipient)

### Architektur

```
        [Plaintext Data]
               │
               │ verschlüsselt mit
               ▼
    ┌─────────────────────┐
    │  Data Encryption    │  ← Zufälliger DEK pro Datensatz
    │  Key (DEK)          │     (AES-256, 32 Bytes)
    └─────────────────────┘
               │
               │ wird verschlüsselt für jeden Empfänger
               ▼
    ┌──────────────────────────────────────────┐
    │  Wrapped Keys (einer pro Empfänger):     │
    │  - DEK_wrapped_alice  (RSA/AES für Alice)│
    │  - DEK_wrapped_bob    (RSA/AES für Bob)  │
    │  - DEK_wrapped_hr     (für Gruppe hr)    │
    └──────────────────────────────────────────┘
```

### Implementierung

```cpp
// Pro verschlüsseltem Feld: Speichere multiple wrapped DEKs
struct EncryptedField {
    std::string ciphertext;       // Mit DEK verschlüsselte Daten
    std::string nonce;            // IV/Nonce für AES-GCM
    
    // Pro authorisierter Gruppe/User: wrapped DEK
    std::map<std::string, std::string> wrapped_keys;
    // z.B. {"hr_team": "base64(RSA_encrypt(DEK, hr_pubkey))",
    //       "admin_group": "base64(RSA_encrypt(DEK, admin_pubkey))"}
};

// Beim Verschlüsseln
std::vector<uint8_t> dek = random_bytes(32);  // Zufälliger DEK
auto ciphertext = AES_GCM_encrypt(plaintext, dek, nonce);

// Wrap DEK für jede autorisierte Gruppe
for (const auto& group : authorized_groups) {
    auto group_pubkey = getGroupPublicKey(group);
    auto wrapped_dek = RSA_encrypt(dek, group_pubkey);
    field.wrapped_keys[group] = base64(wrapped_dek);
}

// Beim Entschlüsseln
auto user_group = claims.groups[0];  // Erste Gruppe des Users
if (!field.wrapped_keys.contains(user_group)) {
    throw UnauthorizedException("No key for your group");
}

auto group_privkey = getGroupPrivateKey(user_group);  // Aus KMS/HSM
auto dek = RSA_decrypt(base64_decode(field.wrapped_keys[user_group]), group_privkey);
auto plaintext = AES_GCM_decrypt(field.ciphertext, dek, field.nonce);
```

### Vorteile
✅ **Feingranular**: Zugriff pro Feld konfigurierbar  
✅ **Einfache Zugriffsänderung**: Neuen Empfänger hinzufügen = neuen wrapped key hinzufügen (kein Re-Encrypt der Daten)  
✅ **User-Austritt sicher**: Wrapped key löschen → kein Zugriff mehr  

### Nachteile
⚠️ **Storage Overhead**: Pro Empfänger ein wrapped key (~256 Bytes bei RSA-2048)  
⚠️ **Komplexität**: Private Keys für Gruppen müssen sicher verwaltet werden (KMS/HSM)  
⚠️ **Performance**: RSA-Decrypt bei jedem Lesevorgang (langsamer als Symmetric)  

---

## Lösung 3: Hybrid (Empfohlen für ThemisDB)

Kombination aus **Gruppen-DEKs** (Lösung 1) für Standard-Zugriff und **Envelope-Encryption** (Lösung 2) für feingranulare Freigaben.

### Standardfall: Gruppen-DEK

```json
{
  "salary": {
    "ciphertext": "...",
    "encryption_group": "hr_team",
    "encryption_type": "group-dek"
  }
}
```

Alle `hr_team`-Mitglieder können lesen/schreiben.

### Sonderfall: Multi-Recipient

```json
{
  "classified_document": {
    "ciphertext": "...",
    "encryption_type": "envelope",
    "wrapped_keys": {
      "police_dept": "...",
      "prosecutors_office": "...",
      "court_judges": "..."
    }
  }
}
```

Nur diese drei Behörden können lesen.

---

## Datenbank-Änderungen

### Schema-Erweiterung

```cpp
// BaseEntity erweitern für Encryption-Metadaten
class EncryptedFieldMetadata {
public:
    enum class Type {
        NONE,           // Unverschlüsselt
        GROUP_DEK,      // Gruppen-basiert (Lösung 1)
        ENVELOPE        // Multi-Recipient (Lösung 2)
    };
    
    Type type;
    std::string encryption_group;  // Für GROUP_DEK
    std::map<std::string, std::string> wrapped_keys;  // Für ENVELOPE
    std::string algorithm = "AES-256-GCM";
    std::string nonce;  // Base64
};
```

### Storage-Format

**Aktuell (BaseEntity):**
```
key: users:alice
value: {
  "pk": "users:alice",
  "name": "Alice",
  "email": "alice@vcc.local"
}
```

**Mit Group-Encryption:**
```
key: users:alice
value: {
  "pk": "users:alice",
  "name": "Alice",
  "salary_encrypted": "base64_ciphertext",
  "salary_group": "hr_team",
  "salary_nonce": "base64_nonce"
}
```

**Mit Envelope-Encryption:**
```
key: users:alice
value: {
  "pk": "users:alice",
  "name": "Alice",
  "ssn_encrypted": "base64_ciphertext",
  "ssn_encryption_type": "envelope",
  "ssn_nonce": "base64_nonce",
  "ssn_wrapped_keys": {
    "hr_team": "base64_wrapped_dek",
    "audit_team": "base64_wrapped_dek"
  }
}
```

### Neue Storage-Keys

```
# Gruppen-DEKs (verschlüsselt mit Master KEK)
key:group:hr_team           → encrypted_dek (32 bytes AES key, encrypted)
key:group:finance_dept      → encrypted_dek
key:group:admin             → encrypted_dek

# Gruppen-Membership-Cache (optional, für Performance)
key:group:hr_team:members   → ["alice", "bob", "charlie"]

# Audit-Log für Key-Rotation
key:group:hr_team:history   → [
  {"version": 1, "created": 1730000000, "rotated": 1730100000},
  {"version": 2, "created": 1730100000, "active": true}
]
```

---

## Implementierungsplan

### Phase 1: Gruppen-DEK-Basis (Einfach)

1. ✅ **PKIKeyProvider erweitern**
   - `getGroupDEK(group_name)` → lädt/erstellt verschlüsselte DEKs
   - `rotateGroupKey(group_name)` → erstellt neuen DEK, markiert alten als deprecated

2. **JWT-Claims-Mapping**
   - `JWTValidator` extrahiert `groups`/`roles` aus Token
   - `AuthMiddleware` prüft Gruppenmitgliedschaft

3. **Schema-Annotation**
   ```json
   {
     "fields": {
       "salary": {
         "type": "int64",
         "encrypted": true,
         "encryption_group": "hr_team"
       }
     }
   }
   ```

4. **QueryEngine-Integration**
   - Bei INSERT/UPDATE: Verschlüssele mit Gruppen-DEK
   - Bei SELECT: Prüfe Gruppenmitgliedschaft, entschlüssele mit Gruppen-DEK

### Phase 2: Envelope-Encryption (Fortgeschritten)

1. **KMS-Integration** (für Gruppen-Keypairs)
   - Generiere RSA-Keypair pro Gruppe
   - Speichere Private Keys in HSM/KMS oder verschlüsselt in DB

2. **Envelope-Wrapping**
   - Implementiere `EnvelopeEncryption` Klasse
   - Wrap/Unwrap DEKs mit RSA/ECIES

3. **API-Erweiterung**
   ```http
   POST /api/v1/data/share
   {
     "document_id": "doc123",
     "grant_access_to": ["police_dept", "prosecutors_office"]
   }
   ```

---

## Empfehlung

**Für VCC-Kontext:**

1. **Start mit Gruppen-DEK (Lösung 1)**
   - Nutze bestehende Keycloak-Gruppen (`hr_team`, `finance_dept`, etc.)
   - Mappt gut auf Behörden-Struktur (Abteilungen, Ämter)
   - Wenig Storage-Overhead

2. **Später Envelope für Sonderfälle (Lösung 2)**
   - Wenn behördenübergreifende Ad-hoc-Freigaben nötig
   - Wenn feingranulare Zugriffskontrolle auf Feld-Ebene gefordert

3. **DB-Änderungen minimal**
   - Nur Metadaten-Felder hinzufügen (`*_encrypted`, `*_group`, `*_nonce`)
   - Keine Breaking Changes für unverschlüsselte Felder
   - Rückwärtskompatibel: `encrypted=false` → wie bisher

**Nächster Schritt:**
Soll ich die Gruppen-DEK-Implementierung prototypisch in `PKIKeyProvider` ergänzen?
