# PII API

Dieses Dokument beschreibt die PII-bezogenen Endpunkte von ThemisDB.

## Ziel

- DSGVO-konforme Pseudonymisierung sensibler Felder (Art. 17/30)
- Selektives Aufdecken (Reveal) geschützter Informationen für berechtigte Nutzer

## Endpunkte

### GET /pii/reveal/{uuid}

- Zweck: Gibt den Originalwert zu einer pseudonymisierten UUID zurück.
- Sicherheit:
  - Erfordert Scope `pii:reveal` oder `admin` (Token-basiert)
  - Zusätzlich Policy-Check (falls PolicyEngine aktiv) mit Action `pii.reveal`
- Antwort:
  - 200 OK: `{"uuid":"<uuid>", "value":"<klartext>"}`
  - 404 Not Found: Mapping nicht vorhanden
  - 403 Forbidden / 401 Unauthorized: Fehlende Berechtigung

Beispiel:

```bash
curl -H "Authorization: Bearer %THEMIS_TOKEN_ADMIN%" \
     http://localhost:8765/pii/reveal/11111111-1111-1111-1111-111111111111
```

### DELETE /pii/{uuid}

- Zweck: DSGVO Art. 17 — Löschen eines PII-Mappings
- Modi:
  - Soft (Standard): Mapping bleibt erhalten, wird aber ausgeblendet (`active=false`), Reveal ist danach nicht mehr möglich
  - Hard: Mapping wird endgültig entfernt (irreversibel)
- Parameter:
  - Query `mode=soft|hard` (optional, Standard: `soft`)
- Sicherheit:
  - Erfordert Scope `pii:erase` oder `admin`
  - Zusätzlich Policy-Check (falls aktiv) mit Action `pii.erase`
- Antwort:
  - 200 OK (immer):
    - Soft: `{"status":"ok|not_found","mode":"soft","uuid":"…","updated":true|false}`
    - Hard: `{"status":"ok|not_found","mode":"hard","uuid":"…","deleted":true|false}`

Beispiel (Soft-Delete, Standard):

```bash
curl -X DELETE -H "Authorization: Bearer %THEMIS_TOKEN_ADMIN%" \
     http://localhost:8765/pii/11111111-1111-1111-1111-111111111111
```

Beispiel (Hard-Delete):

```bash
curl -X DELETE -H "Authorization: Bearer %THEMIS_TOKEN_ADMIN%" \
     "http://localhost:8765/pii/11111111-1111-1111-1111-111111111111?mode=hard"
```

## Implementierung

- Komponente: `utils::PIIPseudonymizer`
  - Speichert Mappings in RocksDB: `pii:mapping:{uuid}`
  - Verschlüsselt Werte mit `security::FieldEncryption` (AES-256-GCM)
  - Audit über `utils::AuditLogger` (Encrypt-then-Sign, optional)
- Server: `HttpServer::handlePiiRevealByUuid`
  - Doppelte Autorisierung: Scope (`pii:reveal` oder `admin`) + Policy (optional)
  - Logging/Audit via AuditLogger wenn konfiguriert
- Server: `HttpServer::handlePiiDeleteByUuid`
  - Soft-Delete: `PIIPseudonymizer::softDeletePII()` → `active=false`, `deleted_at=…`
  - Hard-Delete: `PIIPseudonymizer::erasePII()` → Mapping entfernen
  - Doppelte Autorisierung: Scope (`pii:erase` oder `admin`) + Policy (optional)

## Hinweise

- Schlüsselmanagement: Standardmäßig `MockKeyProvider` (Entwicklung). In Produktion KMS/Vault-Provider verwenden.
- Persistenz der Schlüssel ist notwendig, um Mappings nach Neustart entschlüsseln zu können.
- UUID muss exakt dem beim Pseudonymisieren zurückgegebenen Identifier entsprechen.
