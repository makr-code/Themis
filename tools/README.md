# ThemisDB Admin Tools

## Übersicht

Die ThemisDB Admin Tools sind eine Suite von Windows-Desktop-Anwendungen zur Verwaltung, Überwachung und Compliance-Prüfung der ThemisDB-Datenbank.

## Projekte

### Themis.AdminTools.Shared
Gemeinsam genutzte Bibliothek mit:
- **ThemisApiClient**: HTTP-Client für themis_server REST API
- **Modelle**: DTOs für Audit-Logs, Konfiguration, API-Antworten
- **Utilities**: Wiederverwendbare Hilfsfunktionen

### Themis.AuditLogViewer
WPF-Anwendung zur Anzeige und Analyse von Audit-Logs.

**Features:**
- Zeitbereichsfilter (Von/Bis-Datum)
- Benutzerfilter
- Aktionsfilter
- Entitätstypfilter
- Nur erfolgreiche Aktionen anzeigen
- Seitenweise Navigation (100 Einträge pro Seite)
- CSV-Export
- Moderne WPF-UI mit DataGrid

## Voraussetzungen

- .NET 8 SDK
- Visual Studio 2022 oder VS Code mit C# Dev Kit
- Zugriff auf laufenden themis_server (Standard: http://localhost:8080)

## Installation

```powershell
cd tools
dotnet restore
dotnet build
```

## Konfiguration

Bearbeiten Sie `Themis.AuditLogViewer/appsettings.json`:

```json
{
  "ThemisServer": {
    "BaseUrl": "http://localhost:8080",
    "ApiKey": "",
    "Timeout": 30
  }
}
```

## Ausführen

```powershell
cd Themis.AuditLogViewer
dotnet run
```

## API-Anforderungen

Der themis_server muss folgende Endpunkte bereitstellen:

### GET /api/audit
Query-Parameter:
- `start` (ISO 8601 DateTime)
- `end` (ISO 8601 DateTime)
- `user` (string)
- `action` (string)
- `entity_type` (string)
- `entity_id` (string)
- `success` (boolean)
- `page` (int)
- `page_size` (int)

Antwort:
```json
{
  "entries": [...],
  "totalCount": 1234,
  "page": 1,
  "pageSize": 100,
  "hasMore": true
}
```

### GET /api/audit/export/csv
Gleiche Query-Parameter, gibt CSV-Datei zurück.

## Entwicklung

**Architektur:**
- MVVM-Pattern mit CommunityToolkit.Mvvm
- Dependency Injection (Microsoft.Extensions.DependencyInjection)
- Async/Await für API-Calls
- INotifyPropertyChanged für Data Binding

**Nächste Schritte:**
1. themis_server API-Endpunkte implementieren (C++)
2. Authentifizierung hinzufügen (JWT/API-Key)
3. Weitere Tools entwickeln (siehe tool-todo.md)
4. Deployment-Pipeline einrichten

## Lizenz

Siehe Hauptprojekt-Lizenz.
