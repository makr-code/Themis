# Schlüsselverwaltung (Key Management)

ThemisDB unterstützt eine externe Schlüsselverwaltung via KeyProvider. Aktuell ist standardmäßig ein MockKeyProvider verdrahtet; eine Vault‑basierte Implementierung ist vorbereitet.

**⚠️ Hinweis zur PKI-Integration:** Die aktuelle PKI-Client-Implementierung (`src/utils/pki_client.cpp`) ist ein Demo-Stub für Entwicklung/Tests. Für produktive Nutzung mit eIDAS-konformen Signaturen ist eine echte RSA/OpenSSL-Integration erforderlich. Details siehe `CODE_AUDIT_MOCKUPS_STUBS.md`.

## Schlüsselarten (Beispiele)
- LEK: Local Encryption Key
- KEK: Key Encryption Key
- DEK: Data Encryption Key

Hinweis: Die konkrete Nomenklatur hängt von der Deployment‑Strategie ab (siehe encryption_strategy.md).

## Server‑APIs
- GET /keys – Liste verwalteter Schlüssel
- POST /keys/rotate – Schlüsselrotation auslösen
  - Parameter: key_id (im JSON‑Body `{ "key_id": "DEK" }` oder Query `?key_id=DEK`)
  - Antworten: { success, key_id, new_version }

Fehlerfälle:
- 400 Missing key_id – Schlüssel auswählen
- 503 Keys API not available – KeyProvider nicht initialisiert

## Provider
- MockKeyProvider – zum Testen/Entwickeln
- VaultKeyProvider – vorbereitet (siehe `src/security/vault_key_provider.cpp`), benötigt Vault‑Konfiguration (KV v1/v2, Mount Path, Auth)

## Betrieb
- Absicherung der Endpunkte (Reverse‑Proxy/Firewall/RBAC): Nur autorisierte Admins dürfen /keys/rotate aufrufen.
- Rotation regelmäßig in der Betriebsroutine einplanen (z. B. DEK monatlich, KEK vierteljährlich).
- Überwachung: Anzahl/Versionen der Schlüssel im Admin‑Tool; Alarme für ablaufende Schlüssel.

## Konfiguration
- Basis‑URL des Servers (Reverse‑Proxy ggf. /api → / umschreiben)
- Vault‑Parameter (bei Nutzung): kv_version, kv_mount_path, Auth‑Methode, TLS‑Zertifikate

Siehe auch:
- Admin‑Guide (Routing‑Hinweise)
- encryption_strategy.md / encryption_deployment.md
