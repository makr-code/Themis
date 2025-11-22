# OpenTelemetry Distributed Tracing

**Themis** unterstützt verteiltes Tracing via OpenTelemetry für Production-Debugging und Performance-Analyse.

## Features

- **OTLP HTTP Exporter**: Sendet Traces an Jaeger, Grafana Tempo oder andere OTLP-kompatible Backends
- **RAII Span Management**: Automatisches Span-Lifetime-Management via C++ RAII
- **Conditional Compilation**: Tracing kann zur Build-Zeit deaktiviert werden (kein Runtime-Overhead)
- **Flexible Configuration**: Runtime-Konfiguration via `config.json`

## Architektur

### Komponenten

1. **Tracer Wrapper** (`utils/tracing.h`/`.cpp`)
   - Initialisierung des OTLP HTTP Exporters
   - TracerProvider mit Resource Attributes (service.name, version)
   - SimpleSpanProcessor für sofortigen Export

2. **Span RAII Wrapper**
   - `Tracer::Span`: Move-only Span mit automatischem `end()` im Destruktor
   - `ScopedSpan`: Convenience-Wrapper für lokale Spans
   - Attribute-Support: `setAttribute(key, value)` für string, int64, double, bool

3. **No-Op Fallback**
   - Wenn `THEMIS_ENABLE_TRACING` nicht definiert, sind alle Methoden No-Ops
   - Kein Linking gegen OpenTelemetry-Libraries notwendig

### Datenfluss

```
HTTP Request → ScopedSpan("handleAqlQuery")
                ↓
           QueryEngine::executeQuery() → ScopedSpan("executeQuery")
                ↓
           Index Scans → Child Spans
                ↓
         OTLP HTTP Exporter → Jaeger/Tempo/Collector
```

## Konfiguration

### Build-Zeit

In `CMakeLists.txt`:

```cmake
option(THEMIS_ENABLE_TRACING "Enable OpenTelemetry distributed tracing" ON)
```

Deaktivieren:
```bash
cmake -DTHEMIS_ENABLE_TRACING=OFF ..
```

### Runtime

In `config/config.json`:

```json
{
  "tracing": {
    "enabled": true,
    "service_name": "themis-server",
    "otlp_endpoint": "http://localhost:4318"
  }
}
```

**Wichtig:** `otlp_endpoint` ist der Base-URL des OTLP HTTP Receivers. Der Tracer fügt automatisch `/v1/traces` hinzu.

## Nutzung

### Instrumentierte Komponenten (aktuell)

#### HTTP-Handler (Top-Level Spans)

- **GET /entities/:key**: `entity.key`, `entity.size_bytes`
- **PUT /entities/:key**: `entity.key`, `entity.table`, `entity.pk`, `entity.size_bytes`, `entity.cdc_recorded`
- **DELETE /entities/:key**: `entity.key`, `entity.table`, `entity.pk`, `entity.cdc_recorded`
- **POST /query**: `query.table`, `query.predicates_count`, `query.exec_mode`, `query.result_count`
- **POST /query/aql**: `aql.query`, `aql.explain`, `aql.optimize`, `aql.allow_full_scan`, `aql.result_count`
- **POST /graph/traverse**: `graph.start_vertex`, `graph.max_depth`, `graph.visited_count`
- **POST /vector/search**: `vector.dimension`, `vector.k`, `vector.results_count`

#### QueryEngine (Query Execution Spans)

- **QueryEngine.executeAndKeys**: `query.table`, `query.eq_count`, `query.range_count`, `query.order_by`, `query.result_count`
- **QueryEngine.executeAndEntities**: `query.table`, `query.entities_count`
- **QueryEngine.executeOrKeys**: `query.table`, `query.disjuncts`, `query.result_count`
- **QueryEngine.executeOrEntities**: `query.table`, `query.entities_count`
- **QueryEngine.executeAndKeysSequential**: `query.table`, `query.eq_count`, `query.result_count`
- **QueryEngine.executeAndEntitiesSequential**: `query.table`, `query.entities_count`
- **QueryEngine.fullScan**: `query.table`, `query.eq_count`, `query.range_count`, `fullscan.scanned`, `query.result_count`
- **QueryEngine.executeAndKeysWithFallback**: `query.table`, `query.exec_mode` (full_scan, range_aware, index_optimized, index_parallel, full_scan_fallback), `query.result_count`
- **QueryEngine.executeAndEntitiesWithFallback**: `query.table`, `query.entities_count`
- **QueryEngine.executeAndKeysRangeAware**: `query.table`, `query.range_count`, `query.result_count` oder `query.ordered_count`
- **QueryEngine.executeAndEntitiesRangeAware**: `query.table`, `query.entities_count`

#### AQL Execution Pipeline (Operator-Level Spans)

- **aql.parse**: `aql.query_length` - Parsen der AQL-Query in AST
- **aql.translate** - Übersetzung des AST in ConjunctiveQuery oder TraversalQuery
- **aql.for**: `for.table`, `for.predicates_count`, `for.range_predicates_count`, `for.order_by`, `for.order_desc`, `for.result_count`, `for.exec_mode`
  - Iteriert über Collection (scan oder traversal)
- **aql.filter** - Filterung der Ergebnisse (innerhalb von Traversal/FOR)
  - Bei Traversal: `filter.evaluations_total`, `filter.short_circuits`
- **aql.limit**: `limit.offset`, `limit.count`, `limit.input_count`, `limit.output_count`
  - Begrenzt Ergebnismenge (LIMIT offset, count)
- **aql.collect**: `collect.input_count`, `collect.group_by_count`, `collect.aggregates_count`, `collect.group_count`
  - Gruppierung und Aggregation (GROUP BY, SUM, AVG, COUNT, MIN, MAX)
- **aql.return**: `return.input_count`
  - Finalisiert Ausgabe und serialisiert Entities
- **aql.traversal**: `traversal.start_vertex`, `traversal.min_depth`, `traversal.max_depth`, `traversal.direction`, `traversal.result_count`
  - Child-Span: **aql.traversal.bfs**: `traversal.max_frontier_size_limit`, `traversal.max_results_limit`, `traversal.visited_count`, `traversal.edges_expanded`, `traversal.filter_evaluations`

#### Index-Scans (Child-Spans)

- **index.scanEqual**: `index.table`, `index.column`, `index.result_count`
- **index.scanRange**: `index.table`, `index.column`, `index.result_count`, `range.has_lower`, `range.has_upper`, `range.includeLower`, `range.includeUpper`
- **or.disjunct.execute**: `disjunct.eq_count`, `disjunct.range_count`, `disjunct.result_count`

Diese Spans erlauben die Analyse von Ausführungsmodus, Cardinalities und Hotspots pro Prädikat/Operator.

### Span in HTTP Handler erstellen

```cpp
#include "utils/tracing.h"

void handleAqlQuery(const Request& req, Response& res) {
    auto span = Tracer::ScopedSpan("handleAqlQuery");
    span.setAttribute("http.method", "POST");
    span.setAttribute("http.path", "/aql");
    
    try {
        // ... query execution ...
      span.setAttribute("query.table", tableName);
      span.setStatus(true);
    } catch (const std::exception& e) {
        span.recordError(e.what());
      span.setStatus(false, e.what());
        throw;
    }
}
```

### Child Span erstellen

```cpp
void QueryEngine::executeQuery(...) {
    auto parentSpan = Tracer::ScopedSpan("executeQuery");
    
    {
        auto childSpan = Tracer::startSpan("loadIndexes");
        childSpan.setAttribute("index.count", indexCount);
        // ... index loading ...
    } // childSpan endet automatisch
    
    // ... weiter mit parentSpan ...
}
```

### Fehler aufzeichnen

```cpp
try {
    // ... operation ...
} catch (const std::exception& e) {
    span.recordError(e.what());
     span.setStatus(false, errorMessage);
    throw;
}
```

## Jaeger Integration (Development)

### Jaeger via Docker starten

```bash
docker run -d --name jaeger \
  -p 4318:4318 \
  -p 16686:16686 \
  jaegertracing/all-in-one:latest
```

**Ports:**
- `4318`: OTLP HTTP receiver (für Themis)
- `16686`: Jaeger UI

### Themis konfigurieren

```json
{
  "tracing": {
    "enabled": true,
    "service_name": "themis-dev",
    "otlp_endpoint": "http://localhost:4318"
  }
}
```

### Themis starten

```bash
.\build\Release\themis_server.exe
```

### Traces anzeigen

1. Öffne http://localhost:16686 (Jaeger UI)
2. Wähle Service "themis-dev"
3. Klicke "Find Traces"

## Grafana Tempo Integration (Production)

### Tempo via Docker Compose

```yaml
version: '3'
services:
  tempo:
    image: grafana/tempo:latest
    command: ["-config.file=/etc/tempo.yaml"]
    volumes:
      - ./tempo.yaml:/etc/tempo.yaml
    ports:
      - "4318:4318"   # OTLP HTTP
      - "3200:3200"   # Tempo API
  
  grafana:
    image: grafana/grafana:latest
    ports:
      - "3000:3000"
    environment:
      - GF_AUTH_ANONYMOUS_ENABLED=true
```

**tempo.yaml:**
```yaml
server:
  http_listen_port: 3200

distributor:
  receivers:
    otlp:
      protocols:
        http:
          endpoint: 0.0.0.0:4318

storage:
  trace:
    backend: local
    local:
      path: /tmp/tempo/traces
```

### Grafana Datasource

1. Öffne http://localhost:3000
2. Configuration → Data Sources → Add data source
3. Wähle "Tempo"
4. URL: `http://tempo:3200`
5. Save & Test

## Performance-Hinweise

### Span-Overhead

- **Mit Tracing aktiviert**: ~5-10 µs pro Span (inkl. Attribut-Serialisierung)
- **Ohne Tracing (THEMIS_ENABLE_TRACING=OFF)**: 0 µs (inline no-ops)

### Best Practices

1. **Granularität**: Erstelle Spans für HTTP-Requests, Query-Execution, Index-Scans
2. **Attribute**: Füge relevante Metadaten hinzu (table, index_type, row_count)
3. **Sampling** (zukünftig): Für High-Throughput-Szenarien Sampling verwenden
4. **Batch Processor** (zukünftig): SimpleSpanProcessor → BatchSpanProcessor für bessere Performance

## API-Referenz

### Tracer::initialize()

```cpp
bool Tracer::initialize(const std::string& serviceName, 
                       const std::string& endpoint);
```

Initialisiert den OpenTelemetry Tracer mit OTLP HTTP Exporter.

**Parameter:**
- `serviceName`: Name des Services (erscheint in Jaeger/Tempo)
- `endpoint`: OTLP HTTP Receiver URL (z.B. `http://localhost:4318`)

**Returns:** `true` bei Erfolg, `false` bei Fehler

### Tracer::startSpan()

```cpp
Tracer::Span Tracer::startSpan(const std::string& name);
```

Erstellt einen neuen Span mit dem gegebenen Namen.

**Returns:** Move-only `Span` Objekt

### ScopedSpan

```cpp
Tracer::ScopedSpan span("operationName");
span.setAttribute("key", "value");
```

RAII-Wrapper für automatisches Span-Lifetime-Management.

### Span::setAttribute()

```cpp
void setAttribute(const std::string& key, const std::string& value);
void setAttribute(const std::string& key, int64_t value);
void setAttribute(const std::string& key, double value);
void setAttribute(const std::string& key, bool value);
```

Fügt Attribute zum Span hinzu.

### Span::recordError()

```cpp
void recordError(const std::string& errorMessage);
```

Zeichnet einen Fehler auf und setzt den Span-Status auf `kError`.

### Span::setStatus()

```cpp
void setStatus(StatusCode code);
```

Setzt den Span-Status (`kOk`, `kError`, `kUnset`).

## Troubleshooting

### "Tracer not initialized" Warning

**Symptom:** Log-Meldung "Tracer not initialized, call Tracer::initialize() first"

**Lösung:** Stelle sicher, dass `Tracer::initialize()` vor dem ersten `startSpan()` aufgerufen wird.

### Keine Traces in Jaeger sichtbar

**Prüfe:**
1. Läuft Jaeger? `docker ps | grep jaeger`
2. Ist Tracing aktiviert? `"tracing.enabled": true` in config.json
3. Richtiger Endpoint? `http://localhost:4318` (nicht 4317 für gRPC!)
4. Firewall/Network-Einstellungen

**Debug:**
```bash
# Log-Level auf DEBUG setzen in config.json
"log_level": "debug"

# Themis-Logs prüfen
grep -i "telemetry\|tracer\|span" themis.log
```

### Build-Fehler mit OpenTelemetry

**Symptom:** `error: opentelemetry/... not found`

**Lösung:**
```bash
# vcpkg-Cache löschen und neu installieren
rm -rf build/vcpkg_installed
.\build.ps1
```

## Implementierungsstatus

- ✅ **Infrastruktur**: Tracer-Wrapper, OTLP HTTP Exporter
- ✅ **Configuration**: config.json + CMake-Option
- ✅ **Build**: Kompiliert mit opentelemetry-cpp v1.23.0
- ✅ **Tests**: 303/303 Tests bestanden
- ⚠️ **Instrumentierung**: HTTP-Handler noch nicht instrumentiert
- ⚠️ **Dokumentation**: Deployment-Guide für Production

## Nächste Schritte

1. **HTTP-Handler instrumentieren**: Spans für alle `/aql`, `/vector`, `/graph` Endpoints
2. **Query-Engine instrumentieren**: Child Spans für Index-Scans, Filter-Operationen
3. **Sampling implementieren**: Probabilistic Sampling für High-Throughput
4. **BatchSpanProcessor**: Performance-Optimierung für Production
5. **Context Propagation**: W3C Trace Context für verteilte Systeme

## Siehe auch

- [OpenTelemetry C++ SDK Documentation](https://opentelemetry.io/docs/languages/cpp/)
- [Jaeger Documentation](https://www.jaegertracing.io/docs/)
- [Grafana Tempo Documentation](https://grafana.com/docs/tempo/latest/)
- [OTLP Specification](https://opentelemetry.io/docs/specs/otlp/)
