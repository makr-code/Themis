# Themis Admin Tools – Benutzerhandbuch

Dieses Handbuch beschreibt die sieben Themis Admin-Tools mit einheitlichem Layout (Toolbar | Sidebar | Content | StatusBar) und erklärt die wichtigsten Funktionen und Workflows.

- Einheitliches Branding: Rechts oben „Themis“ (hellblau) öffnet den About-Dialog
- Hamburger-Menü: Links oben einklappbare Sidebar
- StatusBar: Live-Status, Zähler und Ladeindikator

## Gemeinsame Bedienkonzepte
- Themis-Brand-Button: Klick öffnet „Über …“-Dialog mit Version/Build-Hinweisen
- Hamburger-Button: Sidebar ein-/ausblenden
- Suche: Textfeld über dem Grid filtert die angezeigten Einträge
- Export: CSV/PDF/Excel je nach Tool verfügbar

## 1) Audit Log Viewer
- Zweck: Audit-Events sichten, filtern, exportieren
- Wichtige Aktionen: Aktualisieren, CSV-Export
- Filter: Datum, Benutzer, Action, Entity
- Tipps: Multi-Column-Sortierung über Spaltenköpfe nutzen

## 2) SAGA Verifier
- Zweck: SAGA-Batch-Signaturen verifizieren
- Aktionen: Batches laden, Details ansehen, Signatur prüfen, Batch flushen
- Hinweise: Detail-View zeigt Steps/Ergebnisse; Export-Funktion verfügbar

## 3) PII Manager
- Zweck: Verwaltung von UUID↔Pseudonym-Mappings, Art. 17 Löschung, Export
- Aktionen: Laden, Export CSV, Filter zurücksetzen, DSGVO-Löschung per UUID
- Grid-Spalten: OriginalUuid, Pseudonym, CreatedAt, UpdatedAt, Active

## 4) Key Rotation Dashboard
- Zweck: Überblick über LEK/KEK/DEK-Keys, Rotations-Status und manuelle Rotation
- Aktionen: Aktualisieren, Rotieren (gesamt oder je Typ LEK/KEK/DEK)
- Filter: Schlüsseltyp, nur abgelaufene Keys
- StatusBar: Anzahl Schlüssel und abgelaufene in Rot

Backend-Calls:
- Liste laden: GET `/keys`
- Rotation auslösen: POST `/keys/rotate`
	- Parameter: `key_id` im JSON-Body `{ "key_id": "DEK" }` oder als Query `?key_id=DEK`

Fehlerfälle und Hinweise:
- 400 Missing key_id → Bitte Schlüssel auswählen
- 503 Keys API not available → Server-Konfiguration prüfen (KeyProvider)

## 5) Retention Manager
- Zweck: Aufbewahrungs-Policies verwalten und Bereinigung auslösen
- Aktionen: Policies laden, neue Policy erstellen, Bereinigung starten
- Filter: Status (Aktiv/Inaktiv/Abgelaufen), Entitätstyp (AuditLog/Document/…)

## 6) Classification Dashboard
- Zweck: Datenklassifizierung (PUBLIC/INTERNAL/CONFIDENTIAL/RESTRICTED) überwachen und testen
- Aktionen: Aktualisieren, CSV-Export, Compliance-Check, Test-Classification
- Filter: Klassifizierung, Verschlüsselung, Compliance-Status, Gaps-only
- Statistik: Live-Counts in der Sidebar

Backend-Calls:
- Regeln laden: GET `/classification/rules`
- Klassifikation testen: POST `/classification/test`
	- Body: `{ "text": "<Probeinhalt>", "metadata": { "source": "sample" } }`
	- Response: `{ "classification": "CONFIDENTIAL", "confidence": 0.92, "detected_entities": [ { "type": "EMAIL", "value": "a@b.c" } ] }`

Fehlerfälle und Hinweise:
- 400 Missing JSON body → Eingabefeld ausfüllen
- 503 Classification API not available → Server-Konfiguration (PIIDetector) prüfen

## 7) Compliance Reports
- Zweck: Reports (DSGVO, SOX, HIPAA, ISO 27001, PCI-DSS) generieren und exportieren
- Aktionen: Report generieren, Export (PDF/Excel/CSV)
- Einstellungen: Zeitraum, Template (Standard/Detailliert/…); Diagramme/Technikdetails

Backend-Calls:
- Compliance-Übersicht laden: GET `/reports/compliance?type=overview|dsgvo|sox|hipaa|iso27001|pci`
	- Response-Beispiel (gekürzt): `{ "report_type": "overview", "generated_at": "2025-11-02T14:55:00Z", "metrics": { "encrypted_entities": 1234, "pii_findings": 42 } }`

Fehlerfälle und Hinweise:
- 503 Reports API not available → Server-Berechtigungen/Config prüfen

## Starten der Tools
- Bereitstellung: Veröffentlichte EXEs liegen unter `dist/<ToolName>/`
- Start: Doppelklick auf die jeweilige EXE
- Voraussetzung: Themis-Server sollte laufen (Standard: http://localhost:8765)

Routing-Hinweis:
- Falls die Tools `/api/*`-Routen verwenden, richten Sie einen Reverse-Proxy ein, der `/api/` auf den Server-Root `/` rewritet (siehe Admin-Guide). Alternativ die Tool-Konfiguration auf prefix-freie Endpunkte anpassen.

## Häufige Fragen (FAQ)
- Die Sidebar ist weg? → Mit dem ☰-Button oben links wieder einblenden
- Export-Datei nicht sichtbar? → In den Standard-Download-Ordner oder Tool-Verzeichnis schauen; ggf. mit Admin-Rechten starten
- Keine Daten? → Server/Endpoint prüfen, Logs ansehen, Filter zurücksetzen
