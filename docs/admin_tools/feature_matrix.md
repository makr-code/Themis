# ThemisDB Admin Tools - Feature Matrix

## Übersicht der implementierten Features

### ✅ Key Rotation Dashboard

| Feature | Status | Beschreibung |
|---------|--------|--------------|
| Schlüssel-Übersicht | ✅ | Anzeige LEK/KEK/DEK inkl. Version/Status |
| Manuelle Rotation | ✅ | POST `/keys/rotate` mit `key_id` (Body/Query) |
| Filter | ✅ | Nach Typ (LEK/KEK/DEK), „nur abgelaufene“ |
| Status-Updates | ✅ | Zähler gesamt/abgelaufen |
| Fehlerbehandlung | ✅ | 400/503 verständlich anzeigen |

---

### ✅ Classification Dashboard

| Feature | Status | Beschreibung |
|---------|--------|--------------|
| Regeln laden | ✅ | GET `/classification/rules` |
| Test-Classification | ✅ | POST `/classification/test` mit `{ text, metadata }` |
| Export | ✅ | CSV-Export der Ergebnisse |
| Filter | ✅ | Level, Verschlüsselung, Compliance-Status |
| Live-Statistik | ✅ | Counts in Sidebar |

---

### ✅ Compliance Reports

| Feature | Status | Beschreibung |
|---------|--------|--------------|
| Report-Generierung | ✅ | GET `/reports/compliance?type=...` |
| Formate | ✅ | JSON (Server), Export in den Tools (CSV/PDF/Excel) |
| Vorlagen | ✅ | Standard/Detailliert (Tool-seitig) |
| Zeitraum | ✅ | Parametrierbar (Tool-seitig) |
| Fehlerbehandlung | ✅ | 503 „Reports API not available“ verständlich |

---

### ✅ Audit Log Viewer

| Feature | Status | Beschreibung |
|---------|--------|--------------|
| **Datumsfilter** | ✅ | Von/Bis-Datum mit DatePicker |
| **Benutzerfilter** | ✅ | Filter nach Username |
| **Aktionsfilter** | ✅ | Filter nach Action-Typ |
| **Entitätsfilter** | ✅ | Filter nach EntityType |
| **Erfolgsfilter** | ✅ | "Nur erfolgreiche Aktionen" Checkbox |
| **Globale Suche** | ✅ | Durchsucht alle 9 Spalten gleichzeitig |
| **Multi-Column Sort** | ✅ | Sortierung nach ID, Timestamp, User, Action, etc. |
| **Toggle Sort** | ✅ | Klick wechselt aufsteigend/absteigend |
| **Paginierung** | ✅ | Vor/Zurück Buttons, 100 Einträge/Seite |
| **CSV Export** | ✅ | Export gefilterte Daten |
| **Status-Updates** | ✅ | Zeigt gefilterte/gesamt Anzahl |
| **Filter löschen** | ✅ | Reset aller Filter auf Default |
| **ICollectionView** | ✅ | Performante Client-Filter |
| **MVVM Pattern** | ✅ | Clean Architecture |
| **Dependency Injection** | ✅ | Microsoft.Extensions.DI |

**Durchsuchbare Felder:**
- User (Benutzername)
- Action (Aktion)
- EntityType (Entitätstyp)
- EntityId (Entitäts-ID)
- OldValue (Alter Wert)
- NewValue (Neuer Wert)
- IpAddress (IP-Adresse)
- SessionId (Sitzungs-ID)
- ErrorMessage (Fehlermeldung)

---

### ✅ SAGA Verifier

| Feature | Status | Beschreibung |
|---------|--------|--------------|
| **Batch-Liste** | ✅ | Übersicht aller SAGA-Batches |
| **Batch-Suche** | ✅ | Suche nach ID, Hash, Signatur, Timestamp |
| **Batch-Detail** | ✅ | Vollständige Batch-Informationen |
| **SAGA-Steps** | ✅ | Liste aller Schritte im Batch |
| **Step-Suche** | ✅ | Suche nach SAGA ID, Step Name, Status, etc. |
| **Multi-Column Sort** | ✅ | Sortierung für Batches & Steps |
| **Signatur-Verifizierung** | ✅ | Kryptographische Prüfung |
| **Batch Flush** | ✅ | Manuelles Flushen des aktuellen Batches |
| **CSV Export** | ✅ | Export SAGA-Steps |
| **Auto-Load Detail** | ✅ | Automatisches Laden bei Batch-Auswahl |
| **Status-Updates** | ✅ | Zeigt gefilterte/gesamt Anzahl |
| **Split-View** | ✅ | Batch-Liste | Detail-Ansicht |
| **Visual Feedback** | ✅ | ✓/✗ für Verifikations-Status |
| **ICollectionView** | ✅ | Separate Filter für Batches & Steps |
| **MVVM Pattern** | ✅ | CommunityToolkit.Mvvm |

**Batch-Suchfelder:**
- BatchId (Batch-ID)
- Hash (SHA-256 Hash)
- Signature (Kryptographische Signatur)
- Timestamp (Zeitstempel)

**Step-Suchfelder:**
- SagaId (SAGA-ID)
- StepName (Schrittname)
- Status (Status)
- CorrelationId (Korrelations-ID)
- Metadata (Metadaten JSON)

---

## Gemeinsame Features

### Technologie-Stack

| Komponente | Technologie | Version |
|------------|-------------|---------|
| **Framework** | .NET | 8.0 |
| **UI** | WPF | Windows Presentation Foundation |
| **MVVM** | CommunityToolkit.Mvvm | 8.x |
| **DI** | Microsoft.Extensions.DI | 8.x |
| **HTTP** | HttpClient | .NET 8 |
| **Config** | Microsoft.Extensions.Configuration.Json | 8.x |
| **Backend** | C++ themis_server | REST API |

### Architektur-Pattern

✅ **MVVM (Model-View-ViewModel)**
- Klare Trennung UI ↔ Logik
- Data Binding
- Command Pattern
- ObservableObject/ObservableProperty

✅ **Dependency Injection**
- Service Container (App.xaml.cs)
- Singleton ThemisServerConfig
- Transient ViewModels & Windows
- Factory Pattern für HttpClient

✅ **ICollectionView Pattern**
- Client-seitige Filterung
- Keine Änderung der Source-Collection
- Performance-Optimierung
- Kombinierbar mit Sorting

✅ **Repository Pattern**
- ThemisApiClient als Repository
- ApiResponse<T> Wrapper
- Error Handling
- Asynchrone Operationen

### Benutzerfreundlichkeit

✅ **Echtzeit-Suche**
- UpdateSourceTrigger=PropertyChanged
- Instant Feedback
- Keine Server-Anfragen bei Texteingabe

✅ **Visuelle Indikatoren**
- Loading Spinner/Progress Bar
- Status-Leiste mit Meldungen
- Sortier-Pfeile in Spaltenüberschriften
- Platzhalter-Text in Suchfeldern ("🔍 Search...")

✅ **Tastatur-Support**
- Tab-Navigation
- Enter in Suchfeldern
- ESC für Abbrechen

✅ **Responsive Design**
- GridSplitter für variable Layouts
- Auto-Sizing Columns
- ScrollViewer für große Daten
- AlternatingRowBackground für Lesbarkeit

### Performance

✅ **Optimierungen**
- Server-seitige Paginierung (100/Seite)
- Client-seitige Filterung nur auf geladene Daten
- ICollectionView statt Collection-Manipulation
- Async/Await für non-blocking UI
- Background-Threads für I/O

✅ **Memory Management**
- ObservableCollection statt List (für Binding)
- Dispose Pattern in App.OnExit
- ServiceProvider Lifecycle Management

### Fehlerbehandlung

✅ **Exception Handling**
- Try-Catch in allen Commands
- MessageBox für Benutzer-Feedback
- ErrorMessage Property für UI
- StatusMessage für Kontext

✅ **Validation**
- Null-Checks vor API-Calls
- Config-Validation beim Startup
- Filter-Validation (optional vs. required)

---

## REST API Endpoints

### Admin APIs (Keys | Classification | Reports)

| Endpoint | Method | Beschreibung |
|----------|--------|--------------|
| `/keys` | GET | Liste aller gemanagten Schlüssel |
| `/keys/rotate` | POST | Rotation; `key_id` im Body `{ key_id: "DEK" }` oder Query `?key_id=DEK` |
| `/classification/rules` | GET | Liste der Klassifizierungsregeln |
| `/classification/test` | POST | Test-Classification `{ text, metadata }` |
| `/reports/compliance` | GET | Compliance-Report `?type=overview|dsgvo|sox|hipaa|iso27001|pci` |

Hinweis: Einige Tool-Clients verwenden einen `/api`-Prefix (z. B. `/api/keys`). Siehe Admin-Guide für Reverse-Proxy-Rewrite auf prefix-freie Server-Endpunkte.

### Audit API

| Endpoint | Method | Beschreibung |
|----------|--------|--------------|
| `/api/audit` | GET | Liste Audit-Logs mit Filtern |
| `/api/audit/export/csv` | GET | Export als CSV |

**Query-Parameter:**
- `start_date` (DateTime)
- `end_date` (DateTime)
- `user` (string)
- `action` (string)
- `entity_type` (string)
- `success_only` (bool)
- `page` (int)
- `page_size` (int)

### SAGA API

| Endpoint | Method | Beschreibung |
|----------|--------|--------------|
| `/api/saga/batches` | GET | Liste aller SAGA-Batches |
| `/api/saga/batch/{id}` | GET | Batch-Detail mit Steps |
| `/api/saga/batch/{id}/verify` | POST | Verifiziere Signatur |
| `/api/saga/flush` | POST | Flush aktuellen Batch |

---

## Geplante Features (Roadmap)

### Kurzfristig (nächste 2 Wochen)

- [ ] **PII-Manager Tool**
  - UUID ↔ Pseudonym Mapping
  - DSGVO Art. 17 Löschung
  - Export-Funktionen

### Mittelfristig (nächste 4 Wochen)

- [ ] **Retention-Manager**
  - Policy-Konfiguration
  - Überwachung
  - Manuelle Bereinigung

### Langfristig (2-3 Monate)

- [ ] **Erweiterte Admin-Features**
  - Saved Filter Profiles
  - Team-Filter Templates
  - Regex-Support in Suche
  - Advanced Filter Builder (AND/OR)
  - Visualisierung (Charts)

---

## Deployment

### Aktueller Status

✅ **Development Build**
- Debug-Build funktionsfähig
- Local Testing erfolgreich
- themis_server Integration getestet

### Geplant

- [ ] **Release Build**
  - Optimierte Binaries
  - Code-Signierung
  - Installer (MSI/ClickOnce)

- [ ] **Auto-Update**
  - ClickOnce Deployment
  - Version-Check
  - Automatische Updates

- [ ] **Documentation**
  - Benutzerhandbücher
  - Admin-Guide
  - API-Dokumentation
  - Video-Tutorials

---

## Zusammenfassung

**Aktuelle Features:**
- ✅ Admin-Tools: AuditLogViewer, SAGAVerifier, KeyRotation, Classification, Compliance Reports
- ✅ Vollständige Such-, Sortier- und Filterlogik
- ✅ REST API Integration (inkl. Keys, Classification, Reports)
- ✅ MVVM + DI Architecture
- ✅ ICollectionView Performance-Optimierung
- ✅ CSV Export
- ✅ Echtzeit-Suche
- ✅ Multi-Column Sorting
- ✅ Responsive UI

**Nächste Schritte:**
1. PII-Manager Tool entwickeln
2. Retention-Manager erstellen
3. Deployment vorbereiten
4. Dokumentation vervollständigen

**Code-Qualität:**
- ✅ MVVM Pattern konsequent
- ✅ Dependency Injection
- ✅ Error Handling
- ✅ Performance-optimiert
- ✅ Wartbar & erweiterbar
