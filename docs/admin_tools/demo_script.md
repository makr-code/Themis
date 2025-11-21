# ThemisDB Admin Tools - Demo Script

## Demo 1: Audit Log Viewer - Such- und Filterfunktionen (5 Minuten)

### Setup (30 Sekunden)
```
1. themis_server starten (Port 8765)
2. AuditLogViewer.exe öffnen
3. Zeige UI-Overview:
   - Header "ThemisDB Audit Log Viewer"
   - Filter-Bereich (Datum, User, Action, Entity)
   - DataGrid (leer)
   - Status-Leiste "Bereit"
```

### Szenario 1: Basis-Filter (1 Minute)
```
AKTION: Datum-Filter setzen
- Von: Letzte 7 Tage (bereits gesetzt)
- Bis: Heute
- Klick "Laden"

ERWARTUNG:
✓ Loading-Indicator erscheint
✓ DataGrid füllt sich mit Audit-Logs
✓ Status: "X Einträge geladen (Gesamt: Y)"

DEMO-PUNKT:
→ "Standard-Filter lädt Audit-Logs der letzten Woche"
```

### Szenario 2: Echtzeit-Suche (1 Minute)
```
AKTION: Globale Suche verwenden
- Suchfeld: "admin"
- KEINE Taste drücken (auto-update)

ERWARTUNG:
✓ DataGrid filtert sofort
✓ Nur Zeilen mit "admin" (in User, Action, etc.)
✓ Status: "5 von 100 Einträgen angezeigt"

DEMO-PUNKT:
→ "Echtzeit-Suche durchsucht alle Spalten gleichzeitig"
→ "UpdateSourceTrigger=PropertyChanged = Instant Feedback"

AKTION: Suche ändern
- Suchfeld: "create" (überschreibe "admin")

ERWARTUNG:
✓ DataGrid aktualisiert sofort
✓ Nur Zeilen mit "create"
✓ Status: "12 von 100 Einträgen angezeigt"
```

### Szenario 3: Multi-Column Sorting (1 Minute)
```
AKTION: Nach Zeitstempel sortieren
- Klick auf "Zeitstempel" Spaltenüberschrift

ERWARTUNG:
✓ Sortier-Pfeil erscheint (↑ aufsteigend)
✓ Einträge chronologisch sortiert

AKTION: Sortierung umkehren
- Nochmal Klick auf "Zeitstempel"

ERWARTUNG:
✓ Sortier-Pfeil dreht (↓ absteigend)
✓ Neueste Einträge zuerst

DEMO-PUNKT:
→ "Toggle Sort Direction mit einem Klick"

AKTION: Nach anderem Feld sortieren
- Klick auf "Benutzer" Spalte

ERWARTUNG:
✓ Sortierung wechselt zu "Benutzer" (alphabetisch)
✓ Alter Sortier-Pfeil verschwindet
✓ Neuer Sortier-Pfeil bei "Benutzer"
```

### Szenario 4: Kombinierte Filter (1 Minute)
```
AKTION: Filter kombinieren
- Suchfeld: "error"
- Checkbox: "Nur erfolgreiche Aktionen" DEAKTIVIEREN
- Benutzer-Filter: "system"
- Klick "Laden"

ERWARTUNG:
✓ Server-Request mit Filtern
✓ DataGrid zeigt nur:
  - Einträge mit "error" im Text
  - Von Benutzer "system"
  - Inkl. Fehler (Success=false)
✓ Status: "3 von 8 Einträgen angezeigt (Gesamt im System: 1234)"

DEMO-PUNKT:
→ "Server-Filter (User, Datum) + Client-Suche (Suchfeld)"
→ "AND-Verknüpfung aller Filter"
```

### Szenario 5: Export (30 Sekunden)
```
AKTION: CSV Export
- Klick "CSV Export"
- Save-Dialog: "audit_log_20251101_143022.csv"
- Klick "Speichern"

ERWARTUNG:
✓ Loading-Indicator
✓ Success-Dialog: "Daten erfolgreich exportiert"
✓ CSV-Datei mit gefilterten Daten

DEMO-PUNKT:
→ "Export berücksichtigt aktuelle Filter"
→ "Automatischer Dateiname mit Timestamp"
```

### Szenario 6: Filter zurücksetzen (30 Sekunden)
```
AKTION: Filter löschen
- Klick "Filter löschen"

ERWARTUNG:
✓ Datum: Zurück zu "Letzte 7 Tage"
✓ Suchfeld: Leer
✓ Benutzer/Action/Entity: Leer
✓ SuccessOnly: Deaktiviert
✓ DataGrid: Zeigt wieder alle Einträge

DEMO-PUNKT:
→ "Ein Klick zurück zu Standard-Filtern"
```

---

## Demo 2: SAGA Verifier - Batch-Suche und Verifizierung (5 Minuten)

### Setup (30 Sekunden)
```
1. themis_server läuft (Port 8765)
2. SAGAVerifier.exe öffnen
3. Zeige UI-Overview:
   - Header "SAGA Batch Verifier"
   - Toolbar (Refresh, Verify, Flush, Export)
   - Split-View: Batch-Liste | Detail-Ansicht
   - Status-Leiste
```

### Szenario 1: Auto-Load & Batch-Liste (1 Minute)
```
BEIM START:
✓ Auto-Load lädt Batches
✓ Batch-Liste zeigt:
  - Batch ID (kurz)
  - Timestamp (formatiert)
  - Entries (Anzahl)
✓ Status: "Loaded 5 batch(es)"

DEMO-PUNKT:
→ "Automatisches Laden beim Start"
→ "Window.Loaded Event → LoadBatchesCommand"
```

### Szenario 2: Batch-Suche (1 Minute)
```
AKTION: Batch suchen
- Batch-Suchfeld: "abc" (Teil der Batch-ID)

ERWARTUNG:
✓ Batch-Liste filtert sofort
✓ Nur Batches mit "abc" in ID, Hash, Signatur, Timestamp
✓ Status: "2 of 5 batches shown"

DEMO-PUNKT:
→ "Echtzeit-Suche in Batch-Liste"
→ "Durchsucht BatchId, Hash, Signature, Timestamp"

AKTION: Batch-ID sortieren
- Klick auf "Batch ID" Spalte

ERWARTUNG:
✓ Batches alphabetisch sortiert
✓ Sortier-Pfeil erscheint
```

### Szenario 3: Batch-Detail Auto-Load (1 Minute)
```
AKTION: Batch auswählen
- Klick auf ersten Batch in Liste

ERWARTUNG:
✓ Loading-Indicator
✓ Detail-Bereich füllt sich:
  - Batch ID (vollständig)
  - Hash (SHA-256, Monospace-Font)
  - Signature (Kryptographisch, Monospace-Font)
  - Verification: (leer, noch nicht verifiziert)
✓ SAGA-Steps DataGrid:
  - Time, SAGA ID, Step, Status, Correlation ID
  - X Schritte geladen
✓ Status: "Loaded 15 SAGA step(s)"

DEMO-PUNKT:
→ "Auto-Load Detail bei Batch-Auswahl"
→ "OnSelectedBatchChanged → LoadBatchDetailAsync"
```

### Szenario 4: SAGA-Steps Suche (1 Minute)
```
AKTION: Steps durchsuchen
- Step-Suchfeld: "compensation"

ERWARTUNG:
✓ Steps-DataGrid filtert
✓ Nur Steps mit "compensation" in:
  - SagaId, StepName, Status, CorrelationId, Metadata
✓ Status: "3 of 15 steps shown"

DEMO-PUNKT:
→ "Separate Suche für Batches und Steps"
→ "ICollectionView für beide unabhängig"

AKTION: Nach Status sortieren
- Klick auf "Status" Spalte

ERWARTUNG:
✓ Steps nach Status sortiert
✓ z.B. "completed" → "pending" → "failed"
```

### Szenario 5: Signatur-Verifizierung (1 Minute)
```
AKTION: Batch verifizieren
- Button "Verify Selected" klicken

ERWARTUNG:
✓ Loading-Indicator
✓ REST API Call: POST /api/saga/batch/{id}/verify
✓ MessageBox erscheint:
  - Bei Erfolg: "✓ Batch verified successfully"
  - Bei Fehler: "✗ Verification failed: ..."
✓ Detail-Bereich aktualisiert:
  - Verification: "✓ Verified" (grün) / "✗ Failed" (rot)
✓ Status: "✓ Batch verified successfully"

DEMO-PUNKT:
→ "Kryptographische Signatur-Prüfung"
→ "SHA-256 Hash + HMAC-Verification"
→ "Visual Feedback (✓/✗, Farben)"
```

### Szenario 6: Export SAGA-Steps (30 Sekunden)
```
AKTION: Steps exportieren
- Button "Export Steps" klicken
- Save-Dialog: "saga_steps_abc123_20251101_143530.csv"
- Klick "Speichern"

ERWARTUNG:
✓ CSV-Datei mit allen (gefilterten) Steps
✓ Success-Dialog
✓ Status: "Exported 15 steps"

DEMO-PUNKT:
→ "Export berücksichtigt aktuelle Step-Suche"
→ "Batch-ID im Dateinamen"
```

### Szenario 7: Batch Flush (30 Sekunden)
```
AKTION: Aktuellen Batch flushen
- Button "Flush Current" klicken

ERWARTUNG:
✓ Confirmation-Dialog (optional)
✓ REST API Call: POST /api/saga/flush
✓ MessageBox: "Current SAGA batch flushed successfully"
✓ Batch-Liste aktualisiert (neuer Batch erscheint)
✓ Status: "Batch flushed successfully"

DEMO-PUNKT:
→ "Manuelles Flushen erzwingt Batch-Abschluss"
→ "Nützlich für Testing oder Notfall-Situationen"
```

---

## Demo 3: Key Rotation – Schlüssel anzeigen und rotieren (3 Minuten)

### Setup (30 Sekunden)
```
1. themis_server läuft (Port 8765)
2. KeyRotation.exe öffnen
3. UI-Überblick: Schlüssel-Liste, Filter (Typ/abgelaufen), Buttons (Aktualisieren, Rotieren)
```

### Szenario 1: Schlüssel laden (1 Minute)
```
AKTION: Klick „Aktualisieren“

ERWARTUNG:
✓ GET /keys → Liste mit LEK/KEK/DEK
✓ Spalten: KeyId, Version, Status, ExpiresAt
✓ Status: "3 Schlüssel geladen"
```

### Szenario 2: Rotation auslösen (1,5 Minuten)
```
AKTION: Schlüssel „DEK“ auswählen → „Rotieren“ klicken

ERWARTUNG:
✓ POST /keys/rotate mit Body { key_id: "DEK" }
✓ Success-Dialog: "DEK erfolgreich rotiert (neue Version: X)"
✓ Liste aktualisiert → neue Version sichtbar

EDGE CASES:
• 400 Missing key_id → Hinweis im UI
• 503 Keys API not available → Admin-Guide verlinken
```

---

## Demo 4: Classification – Regeln laden und Test-Classification (3 Minuten)

### Setup (30 Sekunden)
```
1. themis_server läuft (Port 8765)
2. ClassificationDashboard.exe öffnen
3. UI-Überblick: Regeln-Panel, Testeingabe, Ergebnisse/Export
```

### Szenario 1: Regeln laden (45 Sekunden)
```
AKTION: Klick „Aktualisieren“

ERWARTUNG:
✓ GET /classification/rules
✓ Anzeige: Name, Muster, Gewichtung
```

### Szenario 2: Klassifikation testen (1,5 Minuten)
```
AKTION: Beispieltext eingeben → „Testen“

ERWARTUNG:
✓ POST /classification/test mit { text, metadata }
✓ Ergebnis: classification=CONFIDENTIAL, confidence ~0.9, detected_entities
✓ Export-Button aktiv

EDGE CASES:
• 400 Missing JSON body → Validierungs-Hinweis
• 503 Classification API not available → Admin-Guide verlinken
```

---

## Demo 5: Compliance Reports – Übersicht abrufen (2 Minuten)

### Setup (15 Sekunden)
```
Tool "ComplianceReports.exe" öffnen
```

### Szenario: Overview-Report (1,5 Minuten)
```
AKTION: Typ „overview“ auswählen → „Generieren“

ERWARTUNG:
✓ GET /reports/compliance?type=overview
✓ Anzeige zentraler Metriken (verschlüsselte Entitäten, PII-Funde, etc.)
✓ Export als CSV/PDF/Excel

EDGE CASES:
• 503 Reports API not available → Hinweis/Retry Option
```

---

## Demo 6: Performance & Benutzerfreundlichkeit (3 Minuten)

### Feature 1: Echtzeit-Feedback (30 Sekunden)
```
DEMO:
1. Suchfeld langsam tippen: "a" → "ad" → "adm" → "admin"
2. Zeige: Filter aktualisiert bei JEDEM Buchstaben
3. Status-Leiste zeigt sofort gefilterte Anzahl

ERKLÄRUNG:
→ "UpdateSourceTrigger=PropertyChanged"
→ "Kein Button-Klick nötig"
→ "Instant Visual Feedback"
```

### Feature 2: ICollectionView Performance (1 Minute)
```
DEMO:
1. Lade 100 Audit-Logs
2. Suche "test" → Filter in <50ms
3. Wechsle zu "admin" → Filter in <50ms
4. Sortiere nach Zeitstempel → Instant

ERKLÄRUNG:
→ "ICollectionView ändert nur Ansicht"
→ "Quell-Collection bleibt unverändert"
→ "Keine Netzwerk-Requests"
→ "Nur bereits geladene Daten betroffen"
```

### Feature 3: Kombinierte Filter (1 Minute)
```
DEMO:
1. Server-Filter: User="admin", Datum=Letzte Woche
2. Klick "Laden" → 50 Einträge vom Server
3. Client-Suche: "create" → 12 von 50 angezeigt
4. Status: "12 von 50 Einträgen angezeigt (Gesamt im System: 1234)"

ERKLÄRUNG:
→ "Server-Filter reduzieren Netzwerk-Traffic"
→ "Client-Suche für Feinabstimmung"
→ "AND-Verknüpfung aller Filter"
→ "Status zeigt 3 Ebenen: Gefiltert / Geladen / Gesamt"
```

### Feature 4: Platzhalter & Tooltips (30 Sekunden)
```
DEMO:
1. Zeige leeres Suchfeld: "🔍 Search..."
2. Hover über Suchfeld: Tooltip "Durchsucht alle Spalten..."
3. Hover über Buttons: Tooltips erklären Funktion

ERKLÄRUNG:
→ "VisualBrush für Platzhalter-Text"
→ "Tooltips für Benutzerfreundlichkeit"
→ "Keine zusätzlichen Labels nötig"
```

---

## Demo 7: Error Handling & Edge Cases (2 Minuten)

### Szenario 1: Server nicht erreichbar (30 Sekunden)
```
SETUP: themis_server beenden

AKTION:
- AuditLogViewer öffnen
- Klick "Laden"

ERWARTUNG:
✓ Loading-Indicator erscheint
✓ Nach Timeout: MessageBox "Fehler beim Laden der Audit-Logs: ..."
✓ Status: "Fehler: Connection refused"
✓ DataGrid bleibt leer

DEMO-PUNKT:
→ "Graceful Error Handling"
→ "Benutzer-freundliche Fehlermeldungen"
```

### Szenario 2: Keine Ergebnisse (30 Sekunden)
```
AKTION:
- Suchfeld: "XYZABCNOTFOUND"

ERWARTUNG:
✓ DataGrid leer
✓ Status: "0 von 100 Einträgen angezeigt (Gesamt: 100)"
✓ KEINE Fehlermeldung (= valider Zustand)

DEMO-PUNKT:
→ "Leere Ergebnisse ≠ Fehler"
→ "Status zeigt deutlich: 0 Treffer"
```

### Szenario 3: Verify ohne Auswahl (30 Sekunden)
```
AKTION:
- SAGAVerifier öffnen
- Klick "Verify Selected" (ohne Batch-Auswahl)

ERWARTUNG:
✓ Button ist DISABLED
✓ Grau ausgegraut
✓ Kein API-Call möglich

DEMO-PUNKT:
→ "IsEnabled Binding verhindert ungültige Aktionen"
→ "Converter: NullToBoolConverter"
```

### Szenario 4: Export ohne Daten (30 Sekunden)
```
AKTION:
- SAGAVerifier öffnen (keine Steps geladen)
- Klick "Export Steps"

ERWARTUNG:
✓ Button ist DISABLED (wegen CountToBoolConverter)
✓ Oder: MessageBox "No SAGA steps to export"

DEMO-PUNKT:
→ "Validation vor Export"
→ "Verhindert leere Dateien"
```

---

## Technische Highlights (für Entwickler-Präsentation)

### Highlight 1: MVVM Pattern
```csharp
// ViewModel (MainViewModel.cs)
[ObservableProperty]
private string _searchText = string.Empty;

partial void OnSearchTextChanged(string value)
{
    _logsView?.Refresh();  // Aktualisiert UI automatisch
    UpdateStatusMessage();
}

// View (MainWindow.xaml)
<TextBox Text="{Binding SearchText, UpdateSourceTrigger=PropertyChanged}"/>
```

### Highlight 2: ICollectionView Filter
```csharp
// Setup
_logsView = CollectionViewSource.GetDefaultView(AuditLogs);
_logsView.Filter = FilterLogs;

// Filter-Logik
private bool FilterLogs(object obj)
{
    if (obj is not AuditLogEntry log)
        return false;

    if (!string.IsNullOrWhiteSpace(SearchText))
    {
        var search = SearchText.ToLowerInvariant();
        return log.User?.ToLowerInvariant().Contains(search) == true ||
               log.Action?.ToLowerInvariant().Contains(search) == true;
    }

    return true;
}
```

### Highlight 3: Dependency Injection
```csharp
// App.xaml.cs
protected override void OnStartup(StartupEventArgs e)
{
    var services = new ServiceCollection();
    
    services.AddSingleton(serverConfig);
    services.AddTransient<ThemisApiClient>(sp => {
        var config = sp.GetRequiredService<ThemisServerConfig>();
        var httpClient = new HttpClient {
            BaseAddress = new Uri(config.BaseUrl),
            Timeout = TimeSpan.FromSeconds(config.Timeout)
        };
        return new ThemisApiClient(httpClient, config);
    });
    services.AddTransient<MainViewModel>();
    services.AddTransient<MainWindow>();
    
    _serviceProvider = services.BuildServiceProvider();
    _serviceProvider.GetRequiredService<MainWindow>().Show();
}
```

### Highlight 4: Async Commands
```csharp
// CommunityToolkit.Mvvm
[RelayCommand]
private async Task LoadBatchesAsync()
{
    IsLoading = true;
    StatusMessage = "Loading SAGA batches...";

    var response = await _apiClient.GetSAGABatchesAsync();

    if (response.Success && response.Data != null)
    {
        Batches = new ObservableCollection<SAGABatchInfo>(response.Data.Batches);
        StatusMessage = $"Loaded {Batches.Count} batch(es)";
    }

    IsLoading = false;
}
```

---

## Zusammenfassung für Präsentation

### Key Messages:
1. **Echtzeit-Suche**: Sofortiges Feedback, keine Button-Klicks
2. **Multi-Column Sort**: Toggle Direction, visueller Feedback
3. **Kombinierte Filter**: Server + Client, AND-Logik
4. **Performance**: ICollectionView, keine Collection-Manipulation
5. **Benutzerfreundlichkeit**: Tooltips, Platzhalter, Status-Updates
6. **Fehlerbehandlung**: Graceful Degradation, klare Meldungen
7. **MVVM**: Clean Architecture, Testbar, Wartbar
8. **DI**: Loose Coupling, konfigurierbar

### Demo-Reihenfolge (10 Minuten):
1. AuditLogViewer - Basis-Funktionen (2 Min)
2. AuditLogViewer - Such & Filter (2 Min)
3. SAGAVerifier - Batch-Suche (2 Min)
4. SAGAVerifier - Verifizierung (2 Min)
5. Performance & UX Highlights (1 Min)
6. Error Handling (1 Min)
