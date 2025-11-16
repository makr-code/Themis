**Plan: Vollständiges Programmierhandbuch aus `src/` ableiten**

Datum: 16. November 2025

Ziel: Für jeden Subordner in `./src/` eine konsolidierte Markdown‑Dokumentation in `./docs/src/<subfolder>/` erzeugen. Jede Datei soll enthalten:
- Kurze Zusammenfassung der Datei / Komponente
- Detaillierte Funktionsbeschreibung (was macht sie, Algorithmen, Seiteneffekte)
- Öffentliche API (Funktionen, Klassen, Signaturen)
- Konfigurationspunkte / Flags
- Abhängigkeiten (andere Module, Libraries, RocksDB CFs)
- Tests / CMake‑Referenzen
- Beispielaufrufe / Usage snippets
- Offene TODOs / Verbesserungs‑Hinweise

Deliverable: Vollständiger Ordner `docs/src/` mit pro‑Subfolder README und pro‑Datei Markdowneinträgen; zusammengefasstes `docs/PROGRAMMING_MANUAL.md` als Inhaltsverzeichnis.

Vorgehensweise (Schritte / Iterationen)
1) Scan & Scaffold (automatisch)
   - Erzeuge `docs/src/<subfolder>/README.md` mit Datei‑Liste und kurze Beschreibungen (aus Kommentar/Header falls vorhanden).
   - Erzeuge `docs/src/<subfolder>/<file>.md` für jede Quell-/Headerdatei mit automatisch extrahierten Signaturen und Platzhaltern.
   - Tool: kleines Python/C++ Skript (siehe Run Steps weiter unten).

2) Auto‑Parsing & Drafts
   - Extrahiere: Datei‑Kommentar (top comment), Klassen, Funktionen, Signaturen, TODOs, #ifdef Flags.
   - Versuche kurze Funktionsbeschreibungen aus Kommentaren zu übernehmen; wenn nicht vorhanden, markiere mit `TODO: describe`.

3) Review & Enrichment (manuell)
   - Entwicklerteam ergänzt algorithmische Details, Komplexität, Seiteneffekte, concurrency notes und Beispiele.

4) Tests & Links
   - Verlinke existierende Tests (in `tests/`) und CMake‑Einträge.
   - Markiere fehlende Tests als TODOs.

5) CI & Publishing
   - Ergänze CI‑Check, der neu hinzugefügte/geänderte `src/` Dateien mit den zugehörigen `docs/src/` Dateien verknüpft (z. B. fehlschlagen, wenn keine entsprechende doc existiert).
   - Commit & PR Workflow: Feature Branch, automatische generation, manuelle Review.

Dateinamenskonvention
- `docs/src/<subfolder>/README.md` — Übersicht + Linkliste
- `docs/src/<subfolder>/<file>.md` — `file.cpp` -> `file.cpp.md` or `file_cpp.md` (keine Pfadkonflikte)

Beispiel‑Skeleton (`docs/src/index/README.md`)
```
# src/index

Kurze Beschreibung des Subsystems.

Enthaltene Dateien:
- `vector_index.cpp` — ANN Index (HNSW optional). TODO: Beschreibung
- `vector_index.h` — API: `VectorIndexManager::init(...)`, `searchKnn(...)` etc.

Siehe auch: `docs/src/index/vector_index.cpp.md` (auto‑draft)
```

Automatisierungs‑Werkzeug (empfohlen)
- Skript: `scripts/generate_src_docs.py`
- Funktionen:
  - Liste alle Subfolders unter `src/`.
  - Für jede Datei: parse header comments, find function signatures (regex), extract TODOs and macros.
  - Output: `docs/src/<subfolder>/README.md` and per‑file md files.

Minimaler Run (lokal)
```powershell
python .\scripts\generate_src_docs.py --src c:\VCC\themis\src --out docs\src
```

Erweiterungen (später)
- Inline code examples extracted from tests
- Cross‑links to `include/` headers
- Automated complexity estimation (lines of code, cyclomatic via lizard)

Anmerkungen zur Priorisierung
- Beginne mit Kern‑Subsystemen: `index`, `content`, `timeseries`, `query`, `server`, `utils`, `cache`.
- Iterativ erweitern, pro Sprint 2–3 Subfolders fertigstellen.

Nächste Schritte (sofort)
1. Genehmigung dieser Vorgehensweise.
2. Ich erstelle die TODOs (geschrieben) — erledigt.
3. Auf Wunsch: ich generiere das Scaffold (Skript + erste automatische Drafts für die Top‑Level Subfolders).

Ende
