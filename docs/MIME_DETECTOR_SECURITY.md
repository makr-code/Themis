# MIME Detector Security & Integrity Konzept

> **⚠️ DEPRECATED**: Dieses In-File-Integrity-Konzept wurde durch ein externes Signatur-System ersetzt.  
> Siehe **[SECURITY_SIGNATURES.md](SECURITY_SIGNATURES.md)** für das aktuelle Design.

## Hintergrund
Ursprünglicher Ansatz: SHA256-Hash direkt in `mime_types.yaml` speichern.  
**Problem**: Henne-Ei-Paradoxon – Hash der Datei ändert die Datei selbst.

## Neue Lösung
- **Externe Signatur-Datenbank** in RocksDB (`security_sig:` Prefix)
- SHA256-Hash der gesamten YAML-Datei wird **vor** dem Parsing berechnet
- Verifikation gegen DB-Signatur entscheidet über `config_verified_` Status
- CRUD-API via `/api/security/signatures` für Signature-Management
- CLI-Tool `scripts/init_mime_signature.py` zur Hash-Initialisierung

## Migration
Alte `integrity`-Blöcke in YAML wurden entfernt. Signaturen werden nun zentral in der Datenbank verwaltet.

Siehe vollständige Dokumentation in **SECURITY_SIGNATURES.md**.
Die MIME-Detection-Konfiguration (Mapping von Dateiendungen, Magic Signatures und Kategorien) ist sicherheitskritisch:
- Falsche oder manipulierte Zuordnungen können zu fehlerhafter Klassifikation führen (z.B. Erkennen von Binärdateien als Text, Umgehung von Sicherheitsprüfungen, ungewollte Verarbeitung).
- Magic Signatures beeinflussen Content-basierte Erkennung und dürfen nicht unbemerkt verändert werden.

Dieses Konzept stellt sicher, dass Änderungen an der YAML-Konfiguration erkannt werden (Integritätsprüfung via SHA256) und dokumentiert den Prozess zur Pflege.

## Komponenten
1. `config/mime_types.yaml` – Primäre Konfigurationsdatei.
2. Integrity-Block in YAML:
   ```yaml
   integrity:
     algorithm: sha256
     hash: <hex>
     generated_at: <UTC Timestamp>
     scope:
       - extensions
       - magic_signatures
       - categories
   ```
3. C++ Klasse `MimeDetector` – Lädt YAML, berechnet deterministischen Hash und verifiziert Integrität.
4. Script `scripts/update_mime_hash.py` – Rechnet Hash neu nach Änderungen und aktualisiert den Integrity-Block.

## Deterministische Serialisierung (Canonical Representation)
Zur Hash-Bildung werden relevante Abschnitte sortiert und als kanonische Zeichenkette zusammengesetzt:
- `[extensions]` gefolgt von Zeilen `extension=mime` alphabetisch sortiert.
- `[magic]` gefolgt von Zeilen `mime@offset=hexbytes[:wildcardPositions]` alphabetisch sortiert.
- `[categories]` gefolgt von Zeilen `category=mime1,mime2,...` (MIME-Liste intern sortiert), alphabetisch sortiert.

Beispiel (gekürzt):
```
[extensions]
json=application/json
png=image/png
[magic]
application/pdf@0=25504446
image/png@0=89504e470d0a1a0a
[categories]
text=application/json,text/plain
```
Dieser String wird mit SHA256 gehasht. Ergebnis ist der `hash`-Wert im Integrity-Block.

## Bedrohungsmodell
| Bedrohung | Wirkung | Abwehr |
|-----------|---------|--------|
| Manipulation einzelner Mappings | Falsche MIME-Erkennung | Hash-Änderung erkannt → `isConfigVerified()==false` |
| Entfernen kritischer Signaturen | Reduzierte Erkennungstreffer | Hash-Änderung erkannt |
| Einfügen bösartiger MIME-Zuordnungen | Umgehung Sicherheitslogik | Hash-Änderung erkannt |
| Replay alter Konfig mit gültigem Hash | Zurückrollen von Patches | Timestamp + Review-Prozess |
| Algorithmus-Downgrade | Schwächere Sicherheit | Erzwingung `sha256` in Code |

## Integritätsprüfung Ablauf
1. Laden YAML.
2. Parsen `extensions`, `magic_signatures`, `categories`.
3. Canonical Serialization erstellen.
4. SHA256 berechnen.
5. Vergleich mit deklarierter `integrity.hash`.
6. Ergebnis in `config_verified_` gespeichert.
   - Erfolgreich: `isConfigVerified()==true` (Info-Log).
   - Fehlgeschlagen: Warn-Log + Anwendung kann Policy anwenden (z.B. Fehlermodus oder eingeschränkter Betrieb).

## Umgang mit Fehlermeldungen
- Fehlender Integrity-Block → Warnung, aber Konfiguration wird geladen.
- Falscher Algorithmus → Warnung, Verifikation übersprungen.
- Hash-Mismatch → Warnung, mögliche Sicherheitsrichtlinie aktivieren (z.B. Reject oder Fallback).

## Pflegeprozess
1. Fachliche Änderung (z.B. neues proprietäres Format hinzufügen).
2. Ausführen:
   ```bash
   python scripts/update_mime_hash.py
   ```
3. Diff prüfen (Hash + Timestamp aktualisiert).
4. Code Review / Security Review.
5. Commit.
6. CI kann optional `isConfigVerified()` testen.

## Erweiterungen / Roadmap
- Optional Signierung mit asymmetrischer Kryptographie (Ed25519) → zusätzlicher `signature`-Block.
- Policy Flag: Bei Hash-Mismatch harte Ablehnung statt Warnung.
- Mehrere Config-Segmente unterstützen (Multi-Datei Hash Merkle-Tree).
- Audit-Log bei jedem Laden mit Hash-Ergebnis.

## Beispiel Policy (Pseudo)
```cpp
MimeDetector det; // lädt config
if (!det.isConfigVerified()) {
    LOG_ERROR("MIME config unverifiziert – starte in Safe-Mode");
    // Safe-Mode: nur allow-list definierte MIME Gruppen zulassen
}
```

## Gründe für SHA256
- Kollisionsresistent für diesen Anwendungsfall ausreichend.
- In OpenSSL vorhanden (keine zusätzliche Abhängigkeit).
- Weit verbreitet, einfach auditierbar.

## Wann Hash erneuern?
- Jede Änderung an: extensions, magic_signatures, categories.
- Nicht erforderlich bei reinen Kommentaranpassungen (Script erkennt aber Änderung am Strukturteil nur, wenn diese Bereiche modifiziert wurden).

## Validierung in Tests
Erweiterung der Unit Tests:
- Erwartung: Bei Platzhalter `UNSIGNED` → `isConfigVerified()==false`.
- Nach Hash-Update → `true`.

## Fazit
Die Integritätsprüfung bietet einfachen, schnellen Schutz vor unbeabsichtigten oder böswilligen Änderungen der MIME-Erkennung – Grundlage für weitere Signaturmechanismen.
