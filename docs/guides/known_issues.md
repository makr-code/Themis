# Known Issues

Datum: 19. Nov 2025

## Filtered Vector Search (QueryEngine::executeFilteredVectorSearch)

- Symptome:
  - Einige GTests liefern 0 Ergebnisse trotz korrekter Pre-Filter-Whitelist (z. B. 60 Kandidaten für `category == "tech"`).
  - Ungefilterte KNN-Suchen und einige kombinierte Filterfälle funktionieren.
- Umgebung:
  - Windows 11, Visual Studio 2022 (MSVC 19.44), vcpkg manifest mode
- Reproduktion:
  - Build:
    ```powershell
    cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
    cmake --build build-msvc --config Debug --target filtered_vector_search_tests
    ```
  - Ausführen (Beispiel eines fehlschlagenden Tests):
    ```powershell
    .\build-msvc\Debug\filtered_vector_search_tests.exe --gtest_filter=FilteredVectorSearchTest.EqualityFilter_Category
    ```
- Beobachtungen/Logs:
  - VectorIndex hat 100 Vektoren (HNSW befüllt)
  - SecondaryIndex scan für `category=tech` liefert 60 PKs
  - `executeFilteredVectorSearch` gibt `status.ok=true`, aber `results.size()==0`
- Verdacht/Nächste Schritte:
  1. Roh-Ergebnisgröße aus `VectorIndexManager::searchKnnPreFiltered` loggen und validieren
  2. Entity-Ladevorgang prüfen (`KeySchema::makeVectorKey(table, pk)`), inkl. Deserialisierung
  3. Post-Filter-Pfade validieren (Feldexistenz, Typkonvertierung für Zahlen)
- Workarounds:
  - Für ungefilterte KNN: `NoFilters_StandardKNN` funktioniert
  - Einige hochselektive oder Mehrfach-Filter-Szenarien liefern korrekte Ergebnisse
- Tracking:
  - Roadmap-Task: "Investigate and fix filtered vector search test failures (Win/MSVC)"
