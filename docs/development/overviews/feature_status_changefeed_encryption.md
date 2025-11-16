```markdown
**Feature‑Status: Changefeed, Batch‑Encryption, Backups, Time‑Series, Search, Security**

Dieses Dokument fasst den aktuellen Implementations‑Status, Fundorte im Sourcecode, Fazit und empfohlene nächste Schritte für mehrere Funktionsbereiche zusammen. Es dient als kompakte Referenz für Entwickler, Release‑Planner und Reviewer.

**Batch‑Encryption Optimierung (encryptEntityBatch)**
- **Status:** Nicht eindeutig implementiert / nur dokumentiert.
- **Fundorte:** Viele Dokumente und Wiki‑Verweise; keine eindeutige Implementationsdatei `FieldEncryption::encryptEntityBatch` oder `field_encryption.cpp` im Source‑Tree gefunden.
- **Fazit:** Architektur und Design sind in der Doku beschrieben; der konkrete Batch‑Codepfad scheint nicht vorhanden zu sein.
- **DoD:** Implementierte Methode `FieldEncryption::encryptEntityBatch(...)` mit folgenden Eigenschaften:
  - **Single base key fetch:** Ein zentraler Schlüsselabruf pro Batch.
  - **HKDF pro Entity:** Für jede Entität eigene Ableitung via vorhandener `derive_cached` HKDF‑API.
  - **Parallelisierung:** Optional mit TBB oder std::thread für Durchsatzsteigerung.
  - **Unit Tests:** Performance + korrekte Decryption Tests.
  - **Files:** neue/erweiterte `src/security/field_encryption.cpp`, Header in `include/security/`.
  - **Geschätzung:** 2–5 Tage (Design + Implementierung + Tests).

... (Document continues — same content moved from previous location)

```
