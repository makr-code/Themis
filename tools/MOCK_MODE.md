# Audit Log Viewer - Schnellstart (Mock-Modus)

## Ohne themis_server testen

Da die themis_server REST API noch nicht implementiert ist, können Sie die App mit Mock-Daten testen:

1. **App.xaml.cs bearbeiten** (Zeile ~30):

```csharp
// Statt:
services.AddHttpClient<ThemisApiClient>();

// Verwenden Sie:
services.AddTransient<MockThemisApiClient>();
```

2. **MainWindowViewModel.cs bearbeiten** (Konstruktor):

```csharp
// Statt:
public MainWindowViewModel(ThemisApiClient apiClient)

// Verwenden Sie:
public MainWindowViewModel(MockThemisApiClient apiClient)
```

3. **Kompilieren und starten:**

```powershell
cd Themis.AuditLogViewer
dotnet run
```

## Features im Mock-Modus

- Generiert 1234 simulierte Audit-Log-Einträge
- Zufällige Benutzer: admin, user1, user2, system, api_client
- Aktionen: CREATE, UPDATE, DELETE, READ, QUERY
- Entitätstypen: Document, Index, Collection, User, Config
- 95% Erfolgsrate (5% simulierte Fehler)
- Filter funktionieren (User, Action, EntityType, Success)
- Export funktioniert
- Paginierung funktioniert (100 Einträge/Seite)

## Zurück zum echten Server

Machen Sie die Änderungen rückgängig und stellen Sie sicher, dass themis_server läuft mit implementierter `/api/audit` Schnittstelle.
