# Qualitätssicherung (QA)

Diese Seite beschreibt die Teststrategie, Werkzeuge und Best Practices zur Sicherstellung der Softwarequalität in ThemisDB: Unit/Integration/E2E-Tests, CI/CD, Code Coverage, statische Analysen und Performance-Regressionstests.

## Testpyramide und Abdeckung

- Unit-Tests: Logiknah, schnell, unabhängig von I/O (z. B. Parser, Serializer, Hilfsfunktionen)
- Integrationstests: Zusammenspiel von Storage/Indexes/Query (z. B. `tests/test_*`)
- E2E-/API-Tests: HTTP-/OpenAPI-Endpunkte, CDC/SSE, Fehlerpfade, Berechtigungen
- Performance-Benchmarks: Google Benchmark (stabil, reproduzierbar), keine CI-Gates, aber Trends überwachen

Zielabdeckung: 70–80% auf kritischen Kernpfaden (Parser, Index, Query Engine, Storage-Wrapper).

## Tests ausführen

- CTest/Executable (Windows PowerShell Beispiel):

```powershell
# Build (Release empfohlen für realistischere Ausführungszeiten)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel

# Tests
ctest --test-dir build --output-on-failure --parallel

# Alternativ: direktes Binary (falls vorhanden)
.\build\Release\themis_tests.exe
```

## Struktur und Namenskonventionen

- Testdateien in `tests/` mit Präfix `test_*.cpp`
- Eine Testdatei pro Komponente/Subsystem
- Klarer Arrange-Act-Assert-Stil, aussagekräftige Namen, keine versteckten Sleeps/Timing-Hacks

## Testdaten-Strategie

- Deterministische Seeds für Zufallsdaten in Tests und Benchmarks
- Kleine, repräsentative Fixtures; große Datensätze nur in Benchmarks/Loadtests
- Cleanup von temporären DB-Pfaden pro Test (z. B. unter `data/test_*`)

## CI/CD-Empfehlungen (GitHub Actions)

- Pipelines (Beispiele):
  - build-and-test: MSVC (Windows) und Clang/GCC (Linux), Release + Debug
  - static-analysis: clang-tidy, cppcheck (Nur-Warnungen, optional blockend für neue Issues)
  - coverage: lcov/gcovr (Linux), Artefakte/Badges veröffentlichen
  - package: Docker-Image bauen, SBOM generieren (Syft), Signatur (cosign) optional
- Caching: vcpkg/ccache (Linux) und MSVC Build Cache
- Artefakte: Testreports (JUnit), Coverage HTML, Binaries (Nightly)

## Code Coverage

- Linux: `-fprofile-arcs -ftest-coverage` bzw. `-fprofile-instr-generate -fcoverage-mapping`
- Werkzeuge: gcovr/lcov; Ausschlüsse für generierten Code/3rd-Party
- Schwellenwerte: z. B. Zeilen ≥ 70%, Funktionen ≥ 75% (nicht hart blockend, aber Trend-basiert)

## Statische Analysen & Linters

- clang-tidy: Modernize-, Performance-, Readability-Checks aktivieren
- cppcheck: Fehleranfällige Muster, MISRA-ähnliche Regeln wo sinnvoll
- Formatierung: clang-format mit projektspezifischem Style; Pre-commit Hook empfohlen

## Performance-Regressionen

- Google Benchmark-Suite regelmäßig auf dedizierter Maschine ausführen
- Relevante Filter: CRUD, MVCC, Pagination, Kompression, Vector-Suche
- Veränderungen dokumentieren (Changelog) und auffällige Deltas untersuchen

## Testbarkeit & Architektur

- Abhängigkeiten injizieren (Interfaces), um I/O zu mocken
- Pure Functions bevorzugen, deterministische Uhrzeit/Zufall abstrahieren
- Kleine, fokussierte Klassen/Methoden, klare Verantwortlichkeiten

## Review-Checkliste (Auszug)

- Korrektheit: Tests vorhanden (Happy Path + 1–2 Edge Cases)
- Robustheit: Fehlerpfade, Timeouts, Null/Empty-Handling
- Performance: Hot Paths, Allokationen, unnötige Kopien, Paging/Batching
- Sicherheit: Input-Validierung, Logging von Secrets vermeiden
- Docs: Öffentliche APIs und Verhalten dokumentiert, Changelog aktualisiert

## Nächste Schritte

- CI-Workflows hinzufügen (GitHub Actions) mit Build+Test, static-analysis, Coverage
- Abdeckungslücken identifizieren (Reports) und kritische Bereiche priorisieren
- Optional: Nightly Performance-Benchmarks mit Trend-Dashboards