# Admin Tools - Such-, Sortier- und Filterlogik

## Übersicht

Alle ThemisDB Admin Tools verfügen über eine vollständige Such-, Sortier- und Filterlogik, um große Datenmengen effizient zu durchsuchen und zu analysieren.

## Implementierte Features

### 1. Echtzeit-Suche (Real-time Search)

**AuditLogViewer:**
- Suchfeld: Durchsucht alle Spalten gleichzeitig
- Durchsuchte Felder:
  - User (Benutzername)
  - Action (Aktion)
  - EntityType (Entitätstyp)
  - EntityId (Entitäts-ID)
  - OldValue (Alter Wert)
  - NewValue (Neuer Wert)
  - IpAddress (IP-Adresse)
  - SessionId (Sitzungs-ID)
  - ErrorMessage (Fehlermeldung)
- **UpdateSourceTrigger=PropertyChanged**: Suche erfolgt automatisch bei jeder Texteingabe
- Case-insensitive: Groß-/Kleinschreibung wird ignoriert

**SAGAVerifier:**
- **Batch-Suche**: Durchsucht Batch-Liste
  - BatchId (Batch-ID)
  - Hash (SHA-256 Hash)
  - Signature (Kryptographische Signatur)
  - Timestamp (Zeitstempel formatiert)
- **Step-Suche**: Durchsucht SAGA-Schritte
  - SagaId (SAGA-ID)
  - StepName (Schrittname)
  - Status (Status)
  - CorrelationId (Korrelations-ID)
  - Metadata (Metadaten)
- Separate Suchfelder für Batches und Steps
- Platzhalter-Text mit 🔍 Icon

### 2. Multi-Column Sorting

**Implementierung:**
- Click-to-Sort: Klick auf Spaltenüberschrift sortiert
- Toggle Sort Direction: Erneuter Klick kehrt Sortierung um
- Visual Feedback: WPF DataGrid zeigt Sortier-Pfeile
- SortMemberPath: Definiert Sortier-Eigenschaft pro Spalte

**AuditLogViewer Sortierbare Spalten:**
- Id (Audit-Log ID)
- Timestamp (Zeitstempel)
- User (Benutzer)
- Action (Aktion)
- EntityType (Entitätstyp)
- EntityId (Entitäts-ID)
- OldValue (Alter Wert)
- NewValue (Neuer Wert)
- Success (Erfolgsstatus)

**SAGAVerifier Sortierbare Spalten:**

*Batches:*
- BatchId (Batch-ID)
- Timestamp (Zeitstempel)
- EntryCount (Anzahl Einträge)

*Steps:*
- Timestamp (Zeitstempel)
- SagaId (SAGA-ID)
- StepName (Schrittname)
- Status (Status)
- CorrelationId (Korrelations-ID)

### 3. Erweiterte Filter

**AuditLogViewer:**
- **Datum-Filter:**
  - Von-Datum (StartDate)
  - Bis-Datum (EndDate)
  - Default: Letzte 7 Tage
- **Text-Filter:**
  - Benutzer (UserFilter)
  - Aktion (ActionFilter)
  - Entitätstyp (EntityTypeFilter)
- **Boolean-Filter:**
  - "Nur erfolgreiche Aktionen" (SuccessOnly)
- **Globale Suche:**
  - Durchsucht alle Spalten gleichzeitig
  - Kombinierbar mit anderen Filtern

**Filter werden kombiniert:**
- Server-seitige Filter: Datum, User, Action, EntityType, SuccessOnly
- Client-seitige Suche: SearchText (nach Laden der Daten)

### 4. ICollectionView Integration

**Technische Implementierung:**

```csharp
// ViewModel Setup
_logsView = CollectionViewSource.GetDefaultView(AuditLogs);
_logsView.Filter = FilterLogs;

// Filter-Methode
private bool FilterLogs(object obj)
{
    if (obj is not AuditLogEntry log)
        return false;

    if (!string.IsNullOrWhiteSpace(SearchText))
    {
        var search = SearchText.ToLowerInvariant();
        var matches = log.User?.ToLowerInvariant().Contains(search) == true ||
                     log.Action?.ToLowerInvariant().Contains(search) == true ||
                     // ... weitere Felder
        
        if (!matches)
            return false;
    }

    return true;
}

// Refresh bei Änderung
partial void OnSearchTextChanged(string value)
{
    _logsView?.Refresh();
    UpdateStatusMessage();
}
```

**Vorteile:**
- ✅ Keine Änderung der Quell-Collection nötig
- ✅ Performance: Nur Ansicht wird aktualisiert
- ✅ Observable Pattern: Automatische UI-Updates
- ✅ Kombinierbar mit Sorting

### 5. Status-Updates

**Intelligente Statusmeldungen:**

```csharp
private void UpdateStatusMessage()
{
    var filtered = AuditLogs.Count(log => _logsView?.Filter == null || _logsView.Filter(log));
    var total = AuditLogs.Count;
    
    if (filtered != total)
    {
        StatusMessage = $"{filtered} von {total} Einträgen angezeigt (Gesamt im System: {TotalCount})";
    }
    else
    {
        StatusMessage = $"{total} Einträge geladen (Gesamt: {TotalCount})";
    }
}
```

**Angezeigte Informationen:**
- Gefilterte Anzahl (wenn Filter aktiv)
- Geladene Anzahl (aktuelle Seite)
- Gesamt-Anzahl im System (alle Seiten)

### 6. Performance-Optimierungen

**Client-seitige Filterung:**
- Filter wird nur auf geladene Daten angewendet (max. 100 Einträge pro Seite)
- Keine Server-Anfrage bei jeder Texteingabe
- Instant Feedback für Benutzer

**Paginierung:**
- Server-seitige Paginierung (100 Einträge pro Seite)
- Reduziert Netzwerk-Traffic
- Schnelle Ladezeiten

**UpdateSourceTrigger:**
- PropertyChanged: Sofortige Suche bei Texteingabe
- Balance zwischen Responsiveness und Performance

## Benutzer-Workflows

### Workflow 1: Schnellsuche nach Benutzeraktionen

```
1. Tool öffnen → AuditLogViewer
2. Standard-Filter → Letzte 7 Tage
3. "Laden" klicken → Daten werden geladen
4. Suchfeld eingeben → "admin" (durchsucht alle Spalten)
5. Ergebnis → Nur Einträge mit "admin" in irgendeinem Feld
6. Spalte klicken → Nach Zeitstempel sortieren
7. Export → Gefilterte Daten als CSV exportieren
```

### Workflow 2: SAGA-Batch Verifizierung finden

```
1. Tool öffnen → SAGAVerifier
2. Auto-Load → Batches werden geladen
3. Batch-Suche → "abc123" (Batch-ID Teilstring)
4. Batch auswählen → Details werden geladen
5. Step-Suche → "compensation" (findet Kompensationsschritte)
6. Step-Spalte sortieren → Nach Status sortieren
7. Verify → Signatur prüfen
8. Export → Schritte als CSV exportieren
```

### Workflow 3: Fehleranalyse

```
1. AuditLogViewer öffnen
2. Datum setzen → Gestern bis Heute
3. Filter: SuccessOnly → DEAKTIVIEREN
4. Laden → Alle Einträge (inkl. Fehler)
5. Suche → "error" oder "exception"
6. Spalte "Success" → Sortieren (Fehler zuerst)
7. Spalte "Timestamp" → Sekundäre Sortierung
8. Analyse → Fehler-Pattern erkennen
```

## Code-Beispiele

### Beispiel 1: Filter-Logik erweitern

```csharp
// Neue Filter-Eigenschaft hinzufügen
[ObservableProperty]
private string _customFilter = string.Empty;

// In FilterLogs-Methode erweitern
private bool FilterLogs(object obj)
{
    if (obj is not AuditLogEntry log)
        return false;

    // Bestehende Suche...
    if (!string.IsNullOrWhiteSpace(SearchText))
    {
        // ...
    }

    // NEUE Filter-Logik
    if (!string.IsNullOrWhiteSpace(CustomFilter))
    {
        if (!log.SomeField?.Contains(CustomFilter) == true)
            return false;
    }

    return true;
}
```

### Beispiel 2: Benutzerdefinierte Sortierung

```csharp
[RelayCommand]
private void CustomSort()
{
    _logsView?.SortDescriptions.Clear();
    
    // Multi-Level Sorting
    _logsView?.SortDescriptions.Add(
        new SortDescription("Success", ListSortDirection.Ascending));
    _logsView?.SortDescriptions.Add(
        new SortDescription("Timestamp", ListSortDirection.Descending));
    
    UpdateStatusMessage();
}
```

### Beispiel 3: Filter kombinieren

```csharp
private bool FilterLogs(object obj)
{
    if (obj is not AuditLogEntry log)
        return false;

    // AND-Verknüpfung: Alle Bedingungen müssen erfüllt sein
    
    // 1. Suchtext-Filter
    if (!string.IsNullOrWhiteSpace(SearchText))
    {
        var search = SearchText.ToLowerInvariant();
        var matches = log.User?.ToLowerInvariant().Contains(search) == true ||
                     log.Action?.ToLowerInvariant().Contains(search) == true;
        
        if (!matches)
            return false; // Nicht gefunden → raus
    }

    // 2. Erfolgs-Filter
    if (SuccessOnlyFilter && !log.Success)
        return false; // Fehler, aber nur Erfolge gewünscht → raus

    // 3. Benutzer-Filter
    if (!string.IsNullOrWhiteSpace(UserFilter))
    {
        if (!log.User?.Contains(UserFilter, StringComparison.OrdinalIgnoreCase) == true)
            return false; // Benutzer passt nicht → raus
    }

    return true; // Alle Filter bestanden → anzeigen
}
```

## Best Practices

### 1. Performance

✅ **DO:**
- Client-seitige Filterung für geladene Daten verwenden
- UpdateSourceTrigger=PropertyChanged für Echtzeit-Suche
- ICollectionView für effiziente Filterung
- Paginierung für große Datenmengen

❌ **DON'T:**
- Nicht bei jeder Texteingabe Server-Request
- Nicht gesamte Datenbank in Memory laden
- Nicht ohne Paginierung arbeiten bei >1000 Einträgen

### 2. Benutzerfreundlichkeit

✅ **DO:**
- Platzhalter-Text in Suchfeldern ("🔍 Search...")
- Tooltips für Suchfunktion
- Status-Updates (gefilterte/gesamte Anzahl)
- Klare visuelle Trennung von Filtern

❌ **DON'T:**
- Nicht ohne Feedback filtern
- Nicht Filter ohne "Löschen"-Button
- Nicht ohne Sortier-Indikatoren

### 3. Code-Qualität

✅ **DO:**
- MVVM Pattern verwenden
- ICollectionView für Filterung
- ObservableCollection für Data Binding
- Partial Methods für Property-Change-Events

❌ **DON'T:**
- Nicht Code-Behind für Filter-Logik
- Nicht direkt Collection manipulieren
- Nicht ohne Property Change Notifications

## Testing

### Unit Tests für Filter-Logik

```csharp
[Fact]
public void FilterLogs_WithSearchText_FiltersCorrectly()
{
    // Arrange
    var viewModel = new MainWindowViewModel(_mockApiClient.Object);
    viewModel.SearchText = "admin";
    
    var log1 = new AuditLogEntry { User = "admin_user" };
    var log2 = new AuditLogEntry { User = "normal_user" };
    
    // Act
    var result1 = viewModel.FilterLogs(log1);
    var result2 = viewModel.FilterLogs(log2);
    
    // Assert
    Assert.True(result1); // admin_user enthält "admin"
    Assert.False(result2); // normal_user enthält nicht "admin"
}
```

### Integration Tests

```csharp
[Fact]
public async Task Search_And_Sort_Integration()
{
    // Arrange
    var viewModel = new MainWindowViewModel(_apiClient);
    await viewModel.LoadBatchesAsync();
    
    // Act - Suche
    viewModel.BatchSearchText = "batch_123";
    
    // Act - Sortierung
    viewModel.SortBatchesCommand.Execute("Timestamp");
    
    // Assert
    var view = CollectionViewSource.GetDefaultView(viewModel.Batches);
    Assert.True(view.SortDescriptions.Count > 0);
    Assert.All(viewModel.Batches, b => 
        Assert.Contains("batch_123", b.BatchId));
}
```

## Zukünftige Erweiterungen

### Geplante Features

1. **Erweiterte Filter-Builder:**
   - UND/ODER-Verknüpfungen
   - Reguläre Ausdrücke
   - Datumsbereich-Presets ("Letzte Woche", "Letzter Monat")

2. **Gespeicherte Filter:**
   - Filter-Profile speichern
   - Favoriten-Filter
   - Team-weite Filter-Templates

3. **Export-Optionen:**
   - Excel-Export mit Formatierung
   - PDF-Reports mit Charts
   - JSON/XML Export

4. **Visualisierung:**
   - Histogram für Zeitstempel-Verteilung
   - Pie-Chart für Action-Verteilung
   - Heatmap für Benutzer-Aktivität

## Zusammenfassung

Die Such-, Sortier- und Filterlogik in den ThemisDB Admin Tools bietet:

✅ Echtzeit-Suche über alle Spalten
✅ Multi-Column Sorting mit Toggle
✅ Kombinierbare Filter (AND-Logik)
✅ ICollectionView für Performance
✅ Intelligente Status-Updates
✅ MVVM-Pattern für Wartbarkeit
✅ Responsive UI (UpdateSourceTrigger)
✅ Export gefilterte Daten

Diese Implementierung ermöglicht es Administratoren, große Datenmengen effizient zu durchsuchen, zu analysieren und zu exportieren.
