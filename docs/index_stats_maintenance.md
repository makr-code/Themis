# Index-Statistiken & Wartung

Dieses Dokument erklärt die Statistik- und Wartungsfunktionen für Indizes.

## IndexStats

```cpp
struct IndexStats {
    std::string type;                 // "regular", "composite", "range", "sparse", "geo", "ttl", "fulltext"
    std::string table;                // Tablename
    std::string column;               // Spaltenname bzw. "col1+col2" für Composite
    size_t entry_count = 0;           // Anzahl Index-Einträge
    size_t estimated_size_bytes = 0;  // grobe Schätzung
    bool unique = false;              // Unique-Constraint
    std::string additional_info;      // Typ-spezifisch (z. B. Composite-Spaltenliste, "sorted", "inverted_index", TTL)
};
```

- Composite: `additional_info` enthält die Spaltenliste (z. B. `"customer_id, status"`).
- Range: `additional_info = "sorted"`
- Fulltext: `additional_info = "inverted_index"`
- TTL: `additional_info = "ttl_seconds=<N>"`

Abruf der Statistiken:

```cpp
SecondaryIndexManager idx(db);

auto s = idx.getIndexStats("users", "email");
auto all = idx.getAllIndexStats("users");
```

## Rebuild eines Index

Rebuild löscht alle bestehenden Indexeinträge eines Indexes und baut sie aus den Entities neu auf.

```cpp
idx.rebuildIndex("users", "email");        // Single-Column
idx.rebuildIndex("orders", "customer_id+status"); // Composite
```

Wann sinnvoll?
- Nach manuellen Eingriffen (inkonsistente Index-Keys)
- Nach Bugfixes im Index-Aufbau

Implementierungsdetails:
- Entities werden unter Prefix `"<table>:"` gescannt (gemäß `KeySchema::makeRelationalKey`).
- Pro Entity wird der passende Index-Key neu erzeugt und gespeichert.

## Reindex der gesamten Tabelle

```cpp
idx.reindexTable("users");
```

- Sucht alle Meta-Keys (`idxmeta:`, `ridxmeta:`, `sidxmeta:`, `gidxmeta:`, `ttlidxmeta:`, `ftidxmeta:`) für die Tabelle und führt `rebuildIndex` je Spalte aus.

## TTL-Cleanup

```cpp
auto [st, removed] = idx.cleanupExpiredEntities("sessions", "last_seen");
```

- Löscht abgelaufene Entities und zugehörige Indexeinträge atomar.
- Muss regelmäßig aufgerufen werden (Timer/Cron).

## Performance-Hinweise

- `estimated_size_bytes` ist eine einfache, konservative Schätzung (Entry-Anzahl * Durchschnittsgröße). Für exakte Größen Messungen auf Key-Ranges durchführen.
- `rebuildIndex` und `reindexTable` sind IO-intensiv. Für große Tabellen ggf. in Wartungsfenstern planen.
- Optionaler Fortschritts-Callback kann ergänzt werden (siehe "Ausblick").

## Ausblick: Progress-Callback (optional)

Geplante API-Idee:

```cpp
void rebuildIndex(
    const std::string& table,
    const std::string& column,
    std::function<bool(size_t done, size_t total)> progress // return false -> abbrechen
);
```

Verwendung:

```cpp
idx.rebuildIndex("users", "email", [](size_t done, size_t total){
  if (done % 10000 == 0) { /* log */ }
  return true; // weiter
});
```
