# Time-Series Engine (TSStore)

Stand: Implementiert und per HTTP-API nutzbar. Diese Seite ist mit dem Quellcode abgeglichen.

## Überblick

Funktionen:
- Speicherung von Zeitreihenpunkten in RocksDB (Schlüssel-Schema `ts:{metric}:{entity}:{timestamp_ms}`)
- Abfragen über Zeitbereiche mit Filter (Metric, Entity, Tags) und Limit
- On-the-fly Aggregationen: min, max, avg, sum, count
- Manuelle Retention (global oder pro Metric)
- Kontinuierliche Aggregationen (abgeleitete Metriken) – MVP-Hilfsklasse
- Optionaler Gorilla-Codec (für zukünftige Blockspeicherung)

Hinweis zur Implementierung:
- Canonical ist `TSStore` (mit Tags/Metadata). `TimeSeriesStore` ist eine einfachere Variante und wird serverseitig nur für Übergangstypen verwendet.

## Komponenten

- TSStore (`include/timeseries/tsstore.h`): Haupt-API (DataPoint, QueryOptions, AggregationResult)
- TimeSeriesStore (`include/timeseries/timeseries.h`): einfache Struktur (nur Wert/Meta), legacy
- RetentionManager (`include/timeseries/retention.h`): setzt per-Metrik-Retention um
- ContinuousAggregateManager (`include/timeseries/continuous_agg.h`): erstellt abgeleitete Metriken in Fenstern
- Gorilla-Codec (Tests/Utils): Kompression für (timestamp,double)

## Datenmodell (TSStore)

Key: `ts:{metric}:{entity}:{timestamp_ms}`

DataPoint:
```json
{
  "metric": "cpu",
  "entity": "server01",
  "timestamp_ms": 1700000000000,
  "value": 0.73,
  "tags": { "env": "prod" },
  "metadata": {}
}
```

QueryOptions:
```json
{
  "metric": "cpu",
  "entity": "server01",      // optional in TSStore, im HTTP-API derzeit erforderlich
  "from_ms": 0,
  "to_ms": 9223372036854775807,
  "limit": 1000,
  "tag_filter": { "env": "prod" } // exakter Match
}
```

AggregationResult:
```json
{
  "min": 0.1,
  "max": 0.9,
  "avg": 0.5,
  "sum": 5.0,
  "count": 10,
  "first_timestamp_ms": 1700000000000,
  "last_timestamp_ms": 1700000060000
}
```

Wichtige Methoden (TSStore):
- `putDataPoint(DataPoint)` / `putDataPoints([...])`
- `query(QueryOptions)` → `(Status, vector<DataPoint>)`
- `aggregate(QueryOptions)` → `(Status, AggregationResult)`
- `getStats()` → `Stats`
- `deleteOldData(cutoff_ms)` / `deleteOldDataForMetric(metric, cutoff_ms)` / `deleteMetric(metric)`

## HTTP-Endpoints (Server)

Zeitreihen sind über folgende Endpunkte nutzbar (Feature-Flag `features.timeseries=true`):

- POST `/ts/put`
  - Body:
    ```json
    { "metric": "cpu", "entity": "srv1", "value": 0.7, "timestamp_ms": 1700000000000, "metadata": { "env": "prod" } }
    ```
  - Antwort: `201 Created` mit `{ success, metric, entity, timestamp_ms }`

- POST `/ts/query`
  - Body:
    ```json
    { "metric": "cpu", "entity": "srv1", "from_ms": 1700000000000, "to_ms": 1700003600000, "limit": 1000 }
    ```
  - Antwort: `200 OK` mit `{ metric, entity, count, data: [ { timestamp_ms, value, tags } ] }`

- POST `/ts/aggregate`
  - Body wie bei `/ts/query` (entity erforderlich)
  - Antwort: `200 OK` mit `{ metric, entity, aggregation: { min,max,avg,sum,count,first_timestamp_ms,last_timestamp_ms } }`

- GET `/ts/config`
  - Gibt aktuelle Kompression- und Chunk-Konfiguration zurück
  - Antwort: `200 OK` mit `{ compression: "gorilla"|"none", chunk_size_hours: 24 }`

- PUT `/ts/config`
  - Ändert Konfiguration zur Laufzeit (betrifft nur neue Datenpunkte)
  - Body:
    ```json
    { "compression": "gorilla", "chunk_size_hours": 24 }
    ```
  - Antwort: `200 OK` mit `{ status: "ok", compression, chunk_size_hours, note }`
  - Kompression-Typen: `"gorilla"` (10-20x Ratio, +15% CPU) oder `"none"`
  - `chunk_size_hours`: 1-168 (max 1 Woche)

Hinweise:
- Tag-Filter sind in der TSStore-API vorhanden (`tag_filter`), in den aktuellen HTTP-Endpunkten aber (noch) nicht explizit verdrahtet.
- Die Server-Handler verwenden intern `TSStore` (`putDataPoint`, `query`, `aggregate`).

## Retention

- Global: `deleteOldData(cutoff_ms)`
- Pro Metric: `deleteOldDataForMetric(metric, cutoff_ms)`
- Manager: `RetentionManager` mit `RetentionPolicy.per_metric[metric] = <Duration>`

Beispiel:
```cpp
RetentionPolicy pol;
pol.per_metric["cpu"] = std::chrono::minutes(30);
pol.per_metric["mem"] = std::chrono::hours(2);
RetentionManager rm(&tsstore, pol);
size_t deleted = rm.apply();
```

## Kontinuierliche Aggregationen (MVP)

- Abgeleitete Metrik: `{metric}__agg_{window_ms}`
- Ein Punkt pro Fensterende; `value = avg`, übrige Kennzahlen in `metadata`

```cpp
ContinuousAggregateManager mgr(&tsstore);
AggConfig cfg{ .metric = "temp", .entity = std::string("sensorA"), .window = {std::chrono::minutes(1)} };
mgr.refresh(cfg, from_ms, to_ms);
```

## Gorilla-Codec (optional)

- Timestamps: Delta-of-Delta, ZigZag + Varint
- Werte: XOR der IEEE‑754 Repräsentation mit Leading/Trailing‑Zero‑Packing

```cpp
GorillaEncoder enc;
enc.add(ts, value);
auto bytes = enc.finish();
GorillaDecoder dec(bytes);
while (auto p = dec.next()) { /* ... */ }
```

## Tests

- `tests/test_tsstore.cpp` – TSStore: CRUD, Query, Aggregation, Stats
- `tests/test_timeseries_retention.cpp` – Retention pro Metric
- `tests/test_gorilla.cpp` – Codec‑Roundtrip/Kompression

## Limitierungen (aktuell)

- Kein automatisches Downsampling/TTL – Retention ist manuell bzw. per Job
- HTTP-Endpunkte erfordern `entity`; TSStore unterstützt zudem Tag‑Filter (noch nicht im Endpoint)
- Kompression standardmäßig deaktiviert (`CompressionType::None`)
