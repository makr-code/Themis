# Vector Operations

Dieses Dokument beschreibt die Vektor-Indexierungs- und Suchoperationen in Themis.

## Übersicht

Der `VectorIndexManager` unterstützt:

- **Batch-Einfügung** (`POST /vector/batch_insert`) für performante Massenimporte
- **Gezielte Löschung** (`DELETE /vector/by-filter`) via PK-Liste oder Key-Präfix
- **KNN-Suche** (`POST /vector/search`) mit optionaler Cursor-Pagination
- **Persistenz** (`POST /vector/index/save`, `POST /vector/index/load`) für HNSW-Index
- **Konfiguration** (`GET/PUT /vector/index/config`) zur Laufzeit (z. B. `efSearch`)
- **Statistiken** (`GET /vector/index/stats`) für Index-Kennzahlen

## Distanzmetriken

Themis unterstützt drei Distanzmetriken für Vektorsuche:

- **L2** (Euklidische Distanz): $d(a,b) = \sqrt{\sum_i (a_i - b_i)^2}$
  - Verwendet für: Absolute Distanzen im Vektorraum
  - "Lower is better" Semantik
  
- **COSINE** (Kosinus-Ähnlichkeit): $d(a,b) = 1 - \frac{a \cdot b}{||a|| \cdot ||b||}$
  - Vektoren werden automatisch normalisiert (L2-Norm)
  - Verwendet für: Richtungsähnlichkeit (z. B. Textembeddings)
  - "Lower is better" Semantik (1 - Kosinus-Ähnlichkeit)

- **DOT** (Skalarprodukt): $d(a,b) = -a \cdot b$
  - **Keine Normalisierung** (Rohwerte werden verwendet)
  - Negiert für "lower is better" Semantik (HNSW verwendet Distanzen)
  - Verwendet für: Maximum Inner Product Search (MIPS), Pre-normalisierte Embeddings
  - **Hinweis**: Bei DOT wird kein Normalisierungsschritt angewendet. Wenn normalisierte Suche gewünscht ist, verwenden Sie COSINE.

**Metrik-Auswahl**: Konfiguriert in `/vector/index/config` via `metric`-Feld:

```json
{
  "metric": "DOT",  // oder "L2", "COSINE"
  "dimension": 768,
  "efSearch": 64
}
```

## Batch Insert

### Endpoint

```
POST /vector/batch_insert
```

### Anfrage

```json
{
  "vector_field": "embedding",  // Standard: "embedding"
  "items": [
    {
      "pk": "doc1",
      "vector": [0.1, 0.2, 0.3],
      "fields": {
        "title": "Beispiel",
        "category": "test"
      }
    },
    {
      "pk": "doc2",
      "vector": [0.4, 0.5, 0.6],
      "fields": {
        "title": "Another",
        "category": "demo"
      }
    }
  ]
}
```

### Antwort

```json
{
  "inserted": 2,
  "errors": 0,
  "objectName": "vectors",
  "dimension": 3
}
```

### Best Practices

- **Batch-Größe**: 100–1000 Einträge pro Request für optimales Latenz/Durchsatz-Verhältnis
- **Auto-Init**: Wenn `dimension` = 0, wird der Index automatisch mit der Dimension des ersten Vektors initialisiert
- **Fehlerbehandlung**: Einzelne fehlerhafte Items werden übersprungen; `errors`-Feld zählt Ausnahmen
- **Transaktionssicherheit**: Jedes Item wird atomar geschrieben (RocksDB WriteBatch)
- **Berechtigungen (wichtig)**: Der Endpoint ist durch die Policy-Engine geschützt. Für erfolgreiche Inserts sind erforderlich:
  - HTTP-Header `Authorization: Bearer <token>`
  - Eine passende Policy-Regel, die den Schreibzugriff auf den Vector-Namespace erlaubt
  - Hinweis: Ohne passenden Token/Policy antwortet der Server mit `{"error":"policy_denied","message":"no_matching_policy"}`

## Delete by Filter

### Endpoint

```
DELETE /vector/by-filter
```

### Anfrage (PK-Liste)

```json
{
  "pks": ["doc1", "doc2", "doc3"]
}
```

### Anfrage (Präfix-Filter)

```json
{
  "prefix": "temp-"
}
```

### Antwort

```json
{
  "deleted": 3,
  "method": "pks"  // oder "prefix"
}
```

### Anwendungsfälle

- **Cleanup**: Löschen temporärer oder veralteter Vektoren via Präfix (z. B. `tmp-`, `staging-`)
- **Bulk-Removal**: Liste spezifischer Dokument-IDs nach Qualitätskontrolle
- **Namensraum-Bereinigung**: Entfernen aller Einträge eines bestimmten Namensraums

## KNN-Suche mit Cursor-Pagination

### Endpoint

```
POST /vector/search
```

### Anfrage (Legacy-Modus)

```json
{
  "vector": [0.1, 0.2, 0.3],
  "k": 10
}
```

### Antwort (Legacy)

```json
{
  "results": [
    {"pk": "doc1", "distance": 0.05},
    {"pk": "doc2", "distance": 0.12}
  ],
  "k": 10,
  "count": 2
}
```

### Anfrage (Cursor-Pagination)

```json
{
  "vector": [0.1, 0.2, 0.3],
  "k": 10,
  "use_cursor": true,
  "cursor": "20"  // optional; Offset der vorherigen Seite
}
```

### Antwort (Cursor-Pagination)

```json
{
  "items": [
    {"pk": "doc21", "distance": 0.08},
    {"pk": "doc22", "distance": 0.09}
  ],
  "batch_size": 2,
  "has_more": true,
  "next_cursor": "30"
}
```

### Best Practices

- **Page-Size**: k = 10–100 für typische UI-Pagination; k = 100–1000 für Batch-Verarbeitung
- **HNSW efSearch**: Setze `efSearch` ≥ k für gute Recall; 64–128 ist ein guter Start
- **Distanz-Metrik**: COSINE (Standard) für normalisierte Embeddings, L2 für räumliche Daten
- **Cursor-Verwendung**: Für große Result-Sets (> k) aktiviere `use_cursor` um Memory-Druck zu reduzieren

## Persistenz

### Speichern

```
POST /vector/index/save
{ "directory": "./data/vector_index" }
```

Speichert:
- `meta.txt`: objectName, dimension, metric, efSearch, M, efConstruction
- `labels.txt`: PK-Mapping (id → PK)
- `index.bin`: HNSW-Struktur (wenn HNSW aktiviert)

### Laden

```
POST /vector/index/load
{ "directory": "./data/vector_index" }
```

Lädt den Index aus persistierten Dateien; überschreibt aktuelle In-Memory-Struktur.

### Auto-Save

Setze `auto_save=true` und `savePath` via `VectorIndexManager::setAutoSavePath()` für automatisches Speichern beim Server-Shutdown.

## Konfiguration zur Laufzeit

### GET /vector/index/config

```json
{
  "objectName": "vectors",
  "dimension": 768,
  "metric": "COSINE",  // oder "L2", "DOT"
  "efSearch": 64,
  "M": 16,
  "efConstruction": 200,
  "hnswEnabled": true
}
```

### PUT /vector/index/config

```json
{
  "efSearch": 128
}
```

**Hinweis**: `M` und `efConstruction` erfordern Index-Rebuild und können zur Laufzeit nicht geändert werden.

## Statistiken

### GET /vector/index/stats

```json
{
  "objectName": "vectors",
  "dimension": 768,
  "metric": "COSINE",  // oder "L2", "DOT"
  "vectorCount": 123456,
  "efSearch": 64,
  "M": 16,
  "efConstruction": 200,
  "hnswEnabled": true
}
```

## Performance-Ziele

| Operation            | Ziel               | Bemerkungen                                    |
|----------------------|---------------------|------------------------------------------------|
| Batch Insert         | < 500 ms / 1000 Items | Mit HNSW M=16, efConstruction=200             |
| KNN Search (k=10)    | < 10 ms             | efSearch=64, ~100k Vektoren                    |
| Delete by PKs (100)  | < 50 ms             | Markiert als gelöscht in HNSW                  |
| Delete by Prefix     | < 200 ms / 1000 Items | Scan + Batch-Delete                            |
| Index Save           | < 2 s / 100k Vectors | Abhängig von IO-Geschwindigkeit                |
| Index Load           | < 1 s / 100k Vectors | Memory-Mapping wenn möglich                    |

## Metriken (Prometheus)

Die folgenden Metriken sind unter `GET /metrics` verfügbar:

- `vccdb_vector_index_size_bytes`: Geschätzte Größe des In-Memory-Index
- `vccdb_vector_search_duration_ms`: Histogram der Suchlatenz in Millisekunden
- `vccdb_vector_batch_insert_duration_ms`: Histogram der Batch-Insert-Latenz
- `vccdb_vector_batch_insert_total`: Counter der gesamten Batch-Insert-Operationen
- `vccdb_vector_batch_insert_items_total`: Counter aller eingefügten Items
- `vccdb_vector_delete_by_filter_total`: Counter der Delete-by-Filter-Operationen
- `vccdb_vector_delete_by_filter_items_total`: Counter aller gelöschten Items

## Häufige Fragen (FAQ)

### Q: Wie gehe ich mit großen Datenmengen um (> 1 Mio. Vektoren)?

A: 
1. Batch-Insert in Blöcken von 500–1000 Items
2. Setze `M=32` und `efConstruction=400` für bessere Qualität (höhere Build-Zeit)
3. Nutze `efSearch=128–200` zur Suche für höhere Recall
4. Aktiviere Auto-Save + regelmäßige Checkpoints
5. Erwäge Sharding (mehrere Indizes) für Skalierung über 10 Mio. Vektoren

### Q: Wie optimiere ich die Suche für niedrige Latenz?

A:
1. Reduziere `efSearch` auf 32–64 (Kompromiss: niedrigere Recall)
2. Setze `k` so niedrig wie möglich (z. B. k=10 statt k=100)
3. Nutze Cursor-Pagination für große Result-Sets
4. Cache häufige Queries (siehe `docs/cdc.md` für Semantic Cache)

### Q: Kann ich mehrere Vektorindizes parallel betreiben?

A: Im aktuellen MVP unterstützt `VectorIndexManager` einen Index pro Instanz. Für mehrere Namensräume:
- Option 1: Separater `VectorIndexManager` pro Namespace (mehrere Server-Instanzen)
- Option 2: Präfix-Trennung im objectName (z. B. `docs_en`, `docs_de`)

### Q: Was passiert bei Dimensionskonflikten?

A: Wenn ein Vektor mit falscher Dimension eingefügt wird:
- Batch-Insert: Item wird übersprungen, `errors`-Counter erhöht
- Single-Insert: Fehler wird sofort zurückgegeben
- Search: Anfrage wird abgelehnt mit HTTP 400

### Q: Wie werden gelöschte Vektoren behandelt?

A:
- HNSW: `markDelete()` markiert Vektoren als gelöscht; physisches Entfernen erfordert Rebuild
- Cache: Sofortige Entfernung aus PK-Mapping und Cache
- RocksDB: Löschung via WriteBatch (kompaktiert in nächster Compaction)

## Beispiele

### 1. Massenimport aus CSV

```python
import csv
import requests
import numpy as np

url = "http://localhost:8765/vector/batch_insert"
batch_size = 500

with open("embeddings.csv") as f:
    reader = csv.DictReader(f)
    batch = []
    for row in reader:
        vec = np.fromstring(row["embedding"], sep=",").tolist()
        batch.append({
            "pk": row["id"],
            "vector": vec,
            "fields": {"title": row["title"]}
        })
        if len(batch) >= batch_size:
            resp = requests.post(url, json={"items": batch})
            print(f"Inserted {resp.json()['inserted']}, errors: {resp.json()['errors']}")
            batch = []
    if batch:
        resp = requests.post(url, json={"items": batch})
        print(f"Final batch: {resp.json()['inserted']} inserted")
```

### 2. Präfix-basierte Bereinigung

```bash
# Alle temporären Vektoren löschen
curl -X DELETE http://localhost:8765/vector/by-filter \
  -H "Content-Type: application/json" \
  -d '{"prefix": "temp-"}'

# Ausgabe: {"deleted": 42, "method": "prefix"}
```

### 3. Paginierte Suche

```python
import requests

url = "http://localhost:8765/vector/search"
query_vec = [0.1, 0.2, 0.3]  # Beispiel-Embedding
cursor = None
all_results = []

while True:
    payload = {"vector": query_vec, "k": 20, "use_cursor": True}
    if cursor:
        payload["cursor"] = cursor
    
    resp = requests.post(url, json=payload).json()
    all_results.extend(resp["items"])
    
    if not resp["has_more"]:
        break
    cursor = resp["next_cursor"]

print(f"Total results: {len(all_results)}")
```

## Siehe auch

- [AQL Syntax](aql_syntax.md) – Hybrid-Queries mit Vektorsuche
- [Indexes](indexes.md) – Sekundär- und Range-Indizes
- [Deployment](deployment.md) – Production-Setup und Tuning
- [Tracing](tracing.md) – Performance-Debugging mit OpenTelemetry
