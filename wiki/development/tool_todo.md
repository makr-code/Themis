# ThemisDB Administration & Compliance Tools - Roadmap

## Übersicht

Diese Roadmap beschreibt die Entwicklung einer Suite von Windows-Desktop-Tools für die Administration, Audit, Compliance und Governance von ThemisDB.

**Ziel:** Bereitstellung benutzerfreundlicher GUI-Anwendungen für Administratoren, Compliance-Officers und Auditoren zur Verwaltung und Überwachung von ThemisDB-Instanzen.

---

## Status-Update (2025-11-02)

Aktueller Stand der Admin-Tools (WPF .NET 8, einheitliches Layout, Branding, Hamburger-Sidebar):

- Themis.SAGAVerifier – Implementiert, baut und läuft
- Themis.AuditLogViewer – Implementiert, baut und läuft (XAML-Strukturfix erledigt)
- Themis.PIIManager – Implementiert; PII-API im Server angebunden; Shared-Client erweitert
- Themis.KeyRotationDashboard – Implementiert (MVP, Demo-Daten)
- Themis.RetentionManager – Implementiert (MVP, Demo-Daten)
- Themis.ClassificationDashboard – Implementiert (MVP, Demo-Daten)
- Themis.ComplianceReports – Implementiert (MVP, Demo-Daten)

Deployment: Self-contained Publish (win-x64) mit zentralem Publish-Skript; Artefakte unter `dist/` erzeugt. Docs (User/Admin Guides) aktualisiert. Security-Audit gestartet (Checkliste + Hardening-Guide vorhanden).

Hinweis: In den Detail-Abschnitten unten sind einige Statusangaben noch auf „Nicht gestartet“. Diese werden sukzessive an den aktuellen Stand angepasst. Für funktionale MVPs sind Backends teils mit Demo-Daten umgesetzt; produktive Server-Endpunkte werden iterativ ergänzt.

## Phase 1: Planung & Architektur

### 1.1 Anforderungsanalyse: Admin/Compliance Tools
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch  
**Geschätzter Aufwand:** 1-2 Tage

**Aufgaben:**
- [ ] Use Cases dokumentieren für jedes geplante Tool
- [ ] Stakeholder-Interviews (IT-Admins, Compliance-Officers, DSB)
- [ ] Prioritätsliste erstellen (welche Tools werden zuerst benötigt?)
- [ ] User Stories definieren (z.B. "Als Compliance-Officer möchte ich...")

**Tools im Scope:**
1. **Audit-Log-Viewer** – Durchsuchen und Analysieren von Audit-Trails
2. **Retention-Policy-Manager** – Verwaltung von Datenaufbewahrungsrichtlinien
3. **Data-Classification-Dashboard** – Übersicht über klassifizierte Daten
4. **PII-Management-Tool** – DSGVO-konforme PII-Verwaltung
5. **Key-Rotation-Dashboard** – Verschlüsselungsschlüssel-Management
6. **SAGA-Transaction-Verifier** – Transaktionslog-Verifikation
7. **Compliance-Report-Generator** – Automatisierte Compliance-Berichte

**Auslieferung:** Anforderungsdokument (Requirements.md)

---

### 1.2 Technologie-Stack festlegen
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch  
**Geschätzter Aufwand:** 2-3 Tage

**Entscheidungskriterien:**
- Entwicklungsgeschwindigkeit
- Wartbarkeit & Langzeitunterstützung
- Integration mit themis_server HTTP-API
- Cross-Platform vs. Windows-native
- Team-Skills (.NET, JavaScript, C++?)

**Optionen:**

| Framework        | Sprache    | Plattform        | Vorteile                          | Nachteile                     |
|------------------|------------|------------------|-----------------------------------|-------------------------------|
| **WPF**          | C# (.NET)  | Windows          | Native Windows, MVVM, DataBinding | Nur Windows                   |
| **WinUI 3**      | C# (.NET)  | Windows 10+      | Modern, Fluent Design, XAML       | Windows 10+ only, neu         |
| **Blazor Hybrid**| C# (.NET)  | Windows/Mac/Linux| Web-Technologie, Cross-Platform   | Performance, Web-Feeling      |
| **Electron**     | JS/TS      | Windows/Mac/Linux| Cross-Platform, Web-Skills        | Große Binaries, RAM-Nutzung   |
| **Qt**           | C++        | Windows/Mac/Linux| High Performance, C++-Integration | Lizenzkosten, Komplexität     |

**Empfehlung:** 
- **Kurzfristig:** WPF (C# .NET 8) – schnelle Entwicklung, gute Integration mit themis_server via HTTP, Team vermutlich .NET-erfahren
- **Langfristig:** Evaluiere WinUI 3 für moderneres UI oder Blazor Hybrid für Cross-Platform

**Aufgaben:**
- [ ] Proof-of-Concept: WPF-App mit REST-API-Call zu themis_server
- [ ] Performance-Test: Grid mit 10.000+ Audit-Log-Einträgen
- [ ] Evaluiere UI-Komponentenbibliotheken (MahApps.Metro, ModernWpf, Syncfusion)

**Auslieferung:** Tech-Stack-Entscheidungsdokument

---

### 1.3 Architektur-Design: Tool-Suite
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch  
**Geschätzter Aufwand:** 3-4 Tage

**Ziel:** Modulare, erweiterbare Architektur mit gemeinsamen Komponenten

**Komponenten:**

```
tools/
├── Themis.AdminTools.Shared/          # Shared Library
│   ├── ApiClient/                     # REST-Client für themis_server
│   │   ├── ThemisApiClient.cs
│   │   ├── Endpoints/
│   │   │   ├── AuditLogEndpoint.cs
│   │   │   ├── RetentionEndpoint.cs
│   │   │   ├── ClassificationEndpoint.cs
│   │   │   ├── PIIEndpoint.cs
│   │   │   ├── KeysEndpoint.cs
│   │   │   └── SAGAEndpoint.cs
│   │   ├── Models/                    # DTOs
│   │   │   ├── AuditLogEntry.cs
│   │   │   ├── RetentionPolicy.cs
│   │   │   └── ...
│   │   └── Auth/
│   │       ├── JwtAuthHandler.cs
│   │       └── ApiKeyAuthHandler.cs
│   ├── UI/                            # Shared UI Components
│   │   ├── Controls/
│   │   │   ├── DateRangePicker.xaml
│   │   │   ├── FilterableDataGrid.xaml
│   │   │   └── StatusIndicator.xaml
│   │   ├── Converters/
│   │   └── Themes/
│   ├── Config/
│   │   ├── ConnectionProfile.cs       # Server-Verbindungseinstellungen
│   │   └── UserPreferences.cs
│   └── Utils/
│       ├── Logger.cs
│       └── Crypto/
│           └── SignatureVerifier.cs   # PKI-Verifikation
│
├── Themis.AuditLogViewer/             # Tool 1
│   ├── ViewModels/
│   ├── Views/
│   └── Program.cs
│
├── Themis.RetentionManager/           # Tool 2
├── Themis.ClassificationDashboard/    # Tool 3
├── Themis.PIIManager/                 # Tool 4
├── Themis.KeyRotationDashboard/       # Tool 5
├── Themis.SAGAVerifier/               # Tool 6
└── Themis.ComplianceReports/          # Tool 7
```

**Architektur-Prinzipien:**
- **MVVM-Pattern** (Model-View-ViewModel) für WPF
- **Dependency Injection** (Microsoft.Extensions.DependencyInjection)
- **Async/Await** für HTTP-Requests
- **Retry-Logic** mit Polly für API-Aufrufe
- **Logging** mit Serilog
- **Configuration** mit appsettings.json

**Aufgaben:**
- [ ] Solution-Struktur in Visual Studio erstellen
- [ ] Shared-Library Grundgerüst implementieren
- [ ] API-Client-Interface definieren
- [ ] DTO-Modelle aus OpenAPI-Spec generieren

**Auslieferung:** Architecture-Decision-Record (ADR.md)

---

## Phase 2: Shared Infrastructure

### 2.1 Shared API-Client-Library entwickeln
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch  
**Geschätzter Aufwand:** 5-7 Tage

**Features:**
- HTTP-Client-Wrapper (HttpClient mit BaseAddress-Config)
- JWT-Authentication (Bearer-Token-Handling)
- API-Key-Authentication (Header: X-API-Key)
- Retry-Policy (3 Retries mit Exponential Backoff)
- Error-Handling (ThemisApiException mit StatusCode/Message)
- Response-Deserialization (System.Text.Json)
- Pagination-Support (für große Audit-Log-Abfragen)

**Endpoints:**

```csharp
// Beispiel: AuditLogEndpoint.cs
public class AuditLogEndpoint
{
    public async Task<PagedResult<AuditLogEntry>> GetLogsAsync(
        DateTime? startTime = null,
        DateTime? endTime = null,
        string userId = null,
        string entityId = null,
        int page = 1,
        int pageSize = 100);
    
    public async Task<Stream> ExportLogsAsync(
        DateTime startTime,
        DateTime endTime,
        ExportFormat format); // CSV, JSON
}

// Beispiel: SAGAEndpoint.cs
public class SAGAEndpoint
{
    public async Task<List<SAGABatch>> GetBatchesAsync();
    public async Task<SAGABatch> GetBatchAsync(string batchId);
    public async Task<bool> VerifyBatchAsync(string batchId);
    public async Task<List<SAGAStep>> GetBatchStepsAsync(string batchId);
}
```

**Aufgaben:**
- [ ] ThemisApiClient-Grundgerüst (BaseClient mit HttpClient)
- [ ] Authentication-Handler (JWT + API-Key)
- [ ] DTO-Modelle für alle Endpoints
- [ ] Unit-Tests mit Mock-Server (WireMock.Net)
- [ ] Integration-Tests gegen echten themis_server
- [ ] NuGet-Package erstellen (Themis.AdminTools.Client)

**Auslieferung:** NuGet-Package + API-Client-Dokumentation

---

## Phase 3: Tool-Entwicklung (MVP)

### 3.1 Tool 1: Audit-Log-Viewer
**Status:** ⬜ Nicht gestartet  
**Priorität:** Sehr hoch (MVP)  
**Geschätzter Aufwand:** 7-10 Tage

**Use Case:** 
IT-Admins und Auditoren müssen Audit-Logs durchsuchen, filtern und exportieren können, um Compliance-Anforderungen zu erfüllen (z.B. "Wer hat wann auf Entität X zugegriffen?").

**Features:**

**Kernfunktionen:**
- ✅ Verbindung zu themis_server konfigurieren (URL, Auth)
- ✅ Audit-Logs in DataGrid anzeigen (Timestamp, User, Action, Entity, Details)
- ✅ Filterung:
  - Zeitbereich (von/bis DateTime-Picker)
  - User-ID (Dropdown mit Autocomplete)
  - Entity-ID (Textfeld mit Wildcard-Support)
  - Action-Typ (Read/Write/Delete Checkboxes)
- ✅ Sortierung (nach jeder Spalte)
- ✅ Pagination (100 Einträge pro Seite)
- ✅ Export:
  - CSV (Excel-kompatibel)
  - JSON (für weitere Verarbeitung)
- ✅ Detailansicht (Doppelklick auf Eintrag → Details-Dialog)

**Erweiterte Features:**
- 🔸 SAGA-Batch-Verifikation:
  - Liste aller SAGA-Batches
  - Verify-Button → PKI-Signatur-Check
  - Tamper-Detection (rot markieren wenn Signatur ungültig)
- 🔸 Echtzeit-Updates (SignalR/WebSocket für neue Logs)
- 🔸 Saved Filters (Favoriten speichern)

**UI-Mockup:**
```
┌─────────────────────────────────────────────────────────────────┐
│ Themis Audit Log Viewer                               [Settings]│
├─────────────────────────────────────────────────────────────────┤
│ Filter:                                                          │
│  From: [2024-10-01] To: [2024-11-01]  User: [All Users ▼]       │
│  Entity: [____________]  Action: ☑Read ☑Write ☑Delete           │
│  [Apply Filter] [Clear] [Export CSV] [Export JSON]              │
├─────────────────────────────────────────────────────────────────┤
│ Timestamp          │ User    │ Action │ Entity      │ Details   │
├────────────────────┼─────────┼────────┼─────────────┼───────────┤
│ 2024-10-15 14:32:11│ alice   │ Write  │ user_123    │ Updated...│
│ 2024-10-15 14:31:05│ bob     │ Read   │ order_456   │ Queried...│
│ ...                │         │        │             │           │
├─────────────────────────────────────────────────────────────────┤
│ Page 1 of 42        [◄ Previous]  [Next ►]         1,234 entries│
└─────────────────────────────────────────────────────────────────┘
```

**Technische Details:**
- ViewModel: `AuditLogViewerViewModel.cs`
- View: `MainWindow.xaml`
- DataGrid: Virtualisierung für Performance bei 10.000+ Einträgen
- Export: CsvHelper-Library, System.Text.Json

**Aufgaben:**
- [ ] MainWindow XAML-Layout
- [ ] ViewModel mit Filter-Properties
- [ ] API-Integration (GetLogsAsync)
- [ ] DataGrid-Binding mit INotifyPropertyChanged
- [ ] Filter-Logik (Where-Clauses)
- [ ] Export-Funktionen (CSV/JSON)
- [ ] Unit-Tests (ViewModel-Logik)
- [ ] UI-Tests (manuelle Testfälle)

**Auslieferung:** Themis.AuditLogViewer.exe

---

### 3.2 Tool 2: Retention-Policy-Manager
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch  
**Geschätzter Aufwand:** 5-7 Tage

**Use Case:**
Compliance-Officers müssen Retention-Policies erstellen, bearbeiten und überwachen, um DSGVO/ISO-27001-Anforderungen zu erfüllen.

**Features:**
- ✅ Liste aller Retention-Policies anzeigen
- ✅ CRUD-Operationen:
  - Create: Neue Policy anlegen (YAML-Editor oder Formular)
  - Read: Policy-Details anzeigen
  - Update: Policy bearbeiten
  - Delete: Policy löschen (mit Bestätigung)
- ✅ Policy-Preview: "Welche Daten werden gelöscht?" (Simulation)
- ✅ Dry-Run: Policy testweise ausführen (ohne tatsächliche Löschung)
- ✅ Scheduling: Integration mit Windows Task Scheduler
- ✅ Export: Compliance-Report (PDF/HTML)

**UI-Mockup:**
```
┌─────────────────────────────────────────────────────────────────┐
│ Retention Policy Manager                                        │
├─────────────────────────────────────────────────────────────────┤
│ Policies:                               [New Policy] [Refresh]  │
│  ├─ user_data_retention (Active)         ├─ [Edit] [Delete]    │
│  │   Collections: ["users"]                                     │
│  │   Retention: 90 days                                         │
│  │   Last Run: 2024-10-30                                       │
│  ├─ audit_log_retention (Active)                                │
│  └─ temp_data_cleanup (Inactive)                                │
├─────────────────────────────────────────────────────────────────┤
│ Policy Editor: user_data_retention                              │
│  Name: [user_data_retention________________]                    │
│  Collections: [users, user_profiles_____]  [Add Collection]     │
│  Retention Period: [90] Days / Months / Years                   │
│  Conditions:                                                     │
│    ☑ Delete if field "deleted_at" is older than retention period│
│    ☑ Anonymize PII fields                                       │
│  [Preview Affected Data] [Dry Run] [Save] [Schedule Task]       │
└─────────────────────────────────────────────────────────────────┘
```

**Aufgaben:**
- [ ] Policy-List-View mit CRUD-Buttons
- [ ] Policy-Editor (YAML oder Form-basiert)
- [ ] Dry-Run-Funktion (API-Call + Result-Grid)
- [ ] Task-Scheduler-Integration (Windows Task Scheduler API)
- [ ] PDF-Export (QuestPDF-Library)
- [ ] Unit-Tests

**Auslieferung:** Themis.RetentionManager.exe

---

### 3.3 Tool 3: Data-Classification-Dashboard
**Status:** ⬜ Nicht gestartet  
**Priorität:** Mittel  
**Geschätzter Aufwand:** 5-7 Tage

**Features:**
- ✅ Pie-Chart: Verteilung PUBLIC/INTERNAL/CONFIDENTIAL/RESTRICTED
- ✅ Histogramm: Daten-Volumen pro Klassifikation
- ✅ Drill-Down: Klick auf Chart → Liste der Entities
- ✅ Batch-Reklassifizierung: Mehrere Entities auf einmal umklassifizieren
- ✅ Export: Klassifikations-Report (Excel/PDF)

**Technologie:** LiveCharts2 oder OxyPlot für WPF-Charts

**Auslieferung:** Themis.ClassificationDashboard.exe

---

### 3.4 Tool 4: PII-Management-Tool
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch (DSGVO-relevant)  
**Geschätzter Aufwand:** 7-10 Tage

**Features:**
- ✅ PII-Scan-Jobs starten (alle Collections durchsuchen)
- ✅ Erkannte PII anzeigen (E-Mail, Telefon, IBAN, etc.)
- ✅ Pseudonymisierungs-Workflow:
  - PII auswählen → Pseudonymize-Button → UUID-Mapping
- ✅ DSGVO Art. 17: Recht auf Vergessenwerden
  - User-ID eingeben → Erase-All-PII-Button
- ✅ Mapping-Übersicht: UUID ↔ Original (verschlüsselt)
- ✅ Consent-Management: PII-Verarbeitungs-Zustimmungen tracken

**Auslieferung:** Themis.PIIManager.exe

---

### 3.5 Tool 5: Key-Rotation-Dashboard
**Status:** ⬜ Nicht gestartet  
**Priorität:** Mittel  
**Geschätzter Aufwand:** 5-7 Tage

**Features:**
- ✅ Liste aller Encryption-Keys (ID, Version, Status, Created, Expires)
- ✅ Rotation-History (Timeline-View)
- ✅ Manual-Rotation-Trigger-Button
- ✅ Key-Health-Check: Warnung bei ablaufenden Keys (< 30 Tage)
- ✅ Integration mit VaultKeyProvider/PKIKeyProvider

**Auslieferung:** Themis.KeyRotationDashboard.exe

---

### 3.6 Tool 6: SAGA-Transaction-Verifier
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch (Audit-Trail-Integrität)  
**Geschätzter Aufwand:** 7-10 Tage

**Features:**
- ✅ SAGA-Batch-Liste mit Signaturen
- ✅ Verify-All-Button: Alle Batches prüfen (PKI-Signatur)
- ✅ Tamper-Detection-Report: Ungültige Signaturen rot markieren
- ✅ Schritt-für-Schritt-Ansicht: Forward/Compensate-Steps anzeigen
- ✅ Compensation-Replay-Simulator: "Was wäre wenn Kompensation ausgeführt würde?"

**Technische Herausforderung:** PKI-Signatur-Verifikation in C# (BouncyCastle oder System.Security.Cryptography)

**Auslieferung:** Themis.SAGAVerifier.exe

---

### 3.7 Tool 7: Compliance-Report-Generator
**Status:** ⬜ Nicht gestartet  
**Priorität:** Mittel  
**Geschätzter Aufwand:** 5-7 Tage

**Features:**
- ✅ Report-Templates:
  - DSGVO-Compliance-Report (PII-Maßnahmen, Retention-Stats)
  - ISO-27001-Checkliste
  - Encryption-Coverage-Report (% verschlüsselte Felder)
- ✅ Automatische Datensammlung via API
- ✅ Export: PDF, HTML, Excel
- ✅ Scheduling: Wöchentliche/Monatliche Reports

**Technologie:** QuestPDF für PDF-Generierung

**Auslieferung:** Themis.ComplianceReports.exe

---

## Phase 4: Deployment & Wartung

### 4.1 UI-Framework Prototyp (1. Tool)
**Status:** ⬜ Nicht gestartet  
**Priorität:** Hoch  
**Geschätzter Aufwand:** 3-5 Tage

**Ziel:** Validiere Tech-Stack mit erstem funktionsfähigen Tool (Audit-Log-Viewer)

**Aufgaben:**
- [ ] End-to-End-Test: Audit-Log-Viewer gegen echten themis_server
- [ ] Performance-Test: 100.000+ Audit-Log-Einträge
- [ ] UI-Responsiveness-Test
- [ ] Sammle User-Feedback (Alpha-Tester)

---

### 4.2 Deployment & Installation
**Status:** ⬜ Nicht gestartet  
**Priorität:** Mittel  
**Geschätzter Aufwand:** 3-5 Tage

**Deployment-Optionen:**

| Methode           | Vorteile                          | Nachteile                   |
|-------------------|-----------------------------------|-----------------------------|
| **MSI-Installer** | Professional, Registry-Integration| Komplex zu erstellen        |
| **ClickOnce**     | Auto-Updates, einfach             | .NET Framework/Core only    |
| **Portable-Exe**  | Kein Installer nötig              | Keine Auto-Updates          |

**Empfehlung:** ClickOnce für schnelle Verteilung + Auto-Updates

**Aufgaben:**
- [ ] ClickOnce-Publishing konfigurieren (Visual Studio)
- [ ] setup.ps1 erstellen:
  ```powershell
  # Installiert alle Tools
  Install-ClickOnceApp "https://themis-tools.example.com/AuditLogViewer"
  Install-ClickOnceApp "https://themis-tools.example.com/RetentionManager"
  # ...
  ```
- [ ] Code-Signing-Zertifikat beantragen (für ClickOnce-Vertrauen)
- [ ] Systemanforderungen dokumentieren:
  - Windows 10 Build 1809+
  - .NET 8.0 Runtime
  - themis_server v1.0+ erreichbar

**Auslieferung:** Installation-Guide.md

---

### 4.3 Dokumentation & User-Guides
**Status:** ⬜ Nicht gestartet  
**Priorität:** Mittel  
**Geschätzter Aufwand:** 5-7 Tage

**Dokumente:**

1. **User-Manuals** (pro Tool):
   - Getting-Started-Guide
   - Feature-Übersicht (mit Screenshots)
   - Workflows (z.B. "Wie exportiere ich Audit-Logs?")
   - Troubleshooting (FAQs)

2. **Admin-Guide:**
   - Installation & Konfiguration
   - themis_server-Verbindungssetup
   - Authentication-Setup (JWT/API-Key)
   - Backup & Restore von Tool-Konfigurationen

3. **Developer-Guide:**
   - API-Client-Library-Dokumentation
   - Wie erstelle ich ein neues Tool?
   - Shared-Components-Übersicht
   - Build & Deployment-Prozess

**Format:** Markdown + GitHub Pages oder Docusaurus

**Aufgaben:**
- [ ] Screenshot-Erstellung (alle Tools)
- [ ] Video-Tutorials (optional)
- [ ] FAQ-Sammlung aus Beta-Tester-Feedback

**Auslieferung:** docs/tools/ (GitHub-Repo)

---

## Zeitplan (Schätzung)

| Phase                        | Dauer      | Abhängigkeiten              |
|------------------------------|------------|-----------------------------|
| **Phase 1: Planung**         | 1 Woche    | -                           |
| **Phase 2: Shared Infra**    | 1 Woche    | Phase 1                     |
| **Phase 3.1: Audit-Log-Viewer** | 2 Wochen | Phase 2                     |
| **Phase 3.2: Retention-Manager** | 1,5 Wochen | Phase 2                  |
| **Phase 3.3-3.7: Weitere Tools** | 5 Wochen | Phase 2, parallel möglich  |
| **Phase 4: Deployment & Docs** | 2 Wochen | Phase 3                     |
| **Gesamt**                   | **~12 Wochen** | (bei 1 Vollzeit-Entwickler) |

**Mit 2 Entwicklern parallel:** ~6-8 Wochen

---

## Prioritäten-Matrix

| Tool                          | Priorität    | Business-Value        | Technische Komplexität |
|-------------------------------|--------------|-----------------------|------------------------|
| Audit-Log-Viewer              | ⭐⭐⭐⭐⭐ | Sehr hoch (Compliance)| Niedrig                |
| SAGA-Transaction-Verifier     | ⭐⭐⭐⭐   | Hoch (Audit-Trail)    | Mittel (PKI)           |
| PII-Management-Tool           | ⭐⭐⭐⭐   | Hoch (DSGVO)          | Mittel                 |
| Retention-Policy-Manager      | ⭐⭐⭐     | Mittel                | Niedrig                |
| Key-Rotation-Dashboard        | ⭐⭐⭐     | Mittel                | Mittel                 |
| Data-Classification-Dashboard | ⭐⭐       | Niedrig               | Niedrig (Charts)       |
| Compliance-Report-Generator   | ⭐⭐       | Niedrig               | Mittel (PDF)           |

**MVP (Minimum Viable Product):**
1. Audit-Log-Viewer
2. SAGA-Transaction-Verifier
3. PII-Management-Tool

---

## Risiken & Mitigationen

| Risiko                                    | Wahrscheinlichkeit | Impact | Mitigation                                      |
|-------------------------------------------|--------------------|---------|-------------------------------------------------|
| API-Änderungen in themis_server           | Mittel             | Hoch    | Versionierung, API-Client mit Breaking-Change-Detection |
| Performance-Probleme bei großen Datensätzen | Mittel           | Mittel  | Pagination, Virtualisierung, Lazy-Loading       |
| UI-Framework-Wahl falsch                  | Niedrig            | Hoch    | Früher Prototyp (Phase 4.1), PoC-Validierung    |
| Deployment-Komplexität (Code-Signing)     | Mittel             | Niedrig | ClickOnce ohne Signing für interne Tests       |

---

## Next Steps

**Sofort:**
1. ✅ Tech-Stack-Entscheidung: WPF vs. WinUI 3 vs. Blazor Hybrid
2. ✅ Anforderungsanalyse: Detaillierte User Stories für Audit-Log-Viewer
3. ✅ Shared-Library-Setup: ThemisApiClient-Grundgerüst

**Nächste Woche:**
- Audit-Log-Viewer MVP implementieren
- API-Client Unit-Tests schreiben
- UI-Mockups für Retention-Manager erstellen

---

## Ressourcen

**Entwicklung:**
- Visual Studio 2022+ (mit WPF-Workload)
- .NET 8.0 SDK
- Git (für Versionskontrolle)

**Libraries:**
- **HTTP:** RestSharp oder native HttpClient
- **JSON:** System.Text.Json
- **UI:** MahApps.Metro (Modern WPF Theme)
- **Charts:** LiveCharts2 oder OxyPlot
- **PDF:** QuestPDF
- **CSV:** CsvHelper
- **Testing:** xUnit, Moq, FluentAssertions

**Tools:**
- Rider oder Visual Studio (IDE)
- Postman (API-Testing)
- WireMock.Net (Mock-Server für Tests)

---

## Change Log

| Datum      | Version | Änderung                              |
|------------|---------|---------------------------------------|
| 2025-11-01 | 1.0     | Initial Roadmap erstellt              |

---

**Autor:** ThemisDB Team  
**Letzte Aktualisierung:** 2025-11-01
