# Themis Admin Tools – Admin-Guide

Dieser Guide richtet sich an Administratoren und beschreibt Bereitstellung, Konfiguration, Betrieb und Troubleshooting der Themis Admin-Tools (Audit, SAGA, Keys, Classification, Reports, Retention, PII).

## Architektur-Überblick
- Frontend: WPF-Tools mit einheitlichem Themis-Layout
- Backend: Themis-Server (Boost.Beast HTTP), Standard-Port: 8765
- Kommunikation: REST-APIs gemäß `docs/openapi.yaml` und ergänzende SSE-Streams

## Build & Deployment
### Self-contained Publish (Win-x64)
- Skript: `publish-all.ps1`
- Ausgabe: `dist/<ProjektName>/`
- Beispiel:
  - Release-Build aller Tools veröffentlichen
  - Artefakte in gemeinsamen `dist`-Ordner ablegen

### Verteilung
- Kopieren Sie die jeweiligen Tool-Ordner aus `dist/<ProjektName>/` auf Zielsysteme
- Optional: Code Signing der EXEs (Empfehlung für produktive Nutzung)

### Update-Prozess
- Erneut `publish-all.ps1` ausführen (Release)
- Verteilte Ordner ersetzen (Downtime beachten, wenn Tools geöffnet sind)

## Konfiguration
- Standard-Server-URL: `http://localhost:8765`
- WICHTIG: Aktuell erwarten die Admin-Tools-HTTP-Clients teils einen `/api`-Prefix (z. B. `/api/keys`). Der Themis-Server stellt die Endpunkte ohne Prefix bereit (z. B. `/keys`). Empfohlene Optionen:
  - Reverse-Proxy (empfohlen): Leiten Sie `/api/*` auf den Server-Root `/` um (Rewrite). Beispiel: Nginx `location /api/ { proxy_pass http://localhost:8765/; rewrite ^/api/(.*)$ /$1 break; }`
  - Alternativ (Folgearbeit): Die Tools auf prefix-freie Routen umstellen.
- Netzwerk: Firewalls/Proxies so konfigurieren, dass der Server erreichbar ist
- Konfigurationsdatei: Basis-URL der Tools zentral konfigurierbar halten (BaseUrl, Timeout)

## Relevante Admin-APIs (für Keys/Classification/Reports)

- Keys Management
  - GET `/keys` → Liste gemanagter Schlüssel
  - POST `/keys/rotate` → Rotation eines Schlüssels; Parameter `key_id` im Body (JSON) oder als Query (`?key_id=...`)
- Classification
  - GET `/classification/rules` → Liste aktiver Klassifizierungsregeln
  - POST `/classification/test` → Testen mit Body `{ "text": "...", "metadata": { ... } }`
- Compliance Reports
  - GET `/reports/compliance?type=overview|dsgvo|sox|hipaa|iso27001|pci`

Hinweis: Die genauen Schemas und Fehlercodes sind in `docs/openapi.yaml` beschrieben. Der SSE-Changefeed ist separat dokumentiert (`docs/apis/openapi.md#sse-streaming-changefeed`).

## Betrieb & Monitoring
- Server-Logs prüfen (`server.err`)
- Health-Check: `GET /health` → 200 OK bei funktionsfähigem Server
- Metriken: `GET /metrics` (Prometheus-Format)
- Tool-Start per Doppelklick aus `dist/...`
- Performance: Self-contained reduziert Abhängigkeiten; ggf. R2R/Trim anpassen

## Sicherheit (Kurzüberblick)
- Code-Signierung der Binaries
- Least-Privilege für Service-Accounts
- TLS-Termination/Reverse-Proxy vor Themis-Server
- Regelmäßige Dependency-Scans (Server/Clients)
- Schlüsselverwaltung: Nur autorisierte Nutzer dürfen `/keys/rotate` verwenden (Absicherung via Reverse-Proxy/Firewall/RBAC)

## Troubleshooting
- Tools zeigen keine Daten
  - Läuft der Server? Hört er auf Port 8765?
  - API-Routen mit Browser/HTTP-Client testen
  - Stimmt das Routing `/api/*` → `/` (Reverse-Proxy)?
  - Filter zurücksetzen, Logs prüfen
- Export schlägt fehl
  - Schreibrechte im Zielverzeichnis
  - Ggf. Admin-Konsole verwenden
- UI wirkt „leer“
  - Server offline / Endpoints liefern 204/404
  - Prüfen, ob Demo-Daten-Backend aktiv ist

## Anhang
- OpenAPI: `docs/openapi.yaml`
- Architekturdokumentation: `docs/architecture.md`, `docs/content_architecture.md`
- Admin-Tools Benutzerhandbuch: `docs/admin_tools_user_guide.md`
