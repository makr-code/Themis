# Hybrid & Fusion Search API

Diese Seite beschreibt die allgemeinen Suchendpunkte, die Vektor- und Volltextsuche kombinieren.

- Basis-URL: `http://<host>:<port>`
- Endpunkte: `/search/hybrid` und `/search/fusion`

## POST /search/hybrid

Kombinierte Suche mit optionaler Graph-Expansion und Filtern. Verwendet intern die Content-Pipeline (Chunks) inkl. optionaler Nachbarschafts-Expansion.

Anfrage (JSON):
```json
{
  "query": "string",
  "k": 10,
  "expand": { "hops": 1 },
  "filters": { "category": "TEXT" },
  "scoring": { "alpha": 1.0, "beta": 0.2, "gamma": 0.1 },
  "tie_break": "pk",
  "tie_break_epsilon": 1e-12
}
```

Parameter:
- `query` (string, erforderlich): Suchtext (Embedding wird intern erzeugt)
- `k` (int, optional, Default 10): Anzahl der Ergebnisse
- `expand.hops` (int, optional, Default 1): Anzahl der Graph-Hops für Kontext-Expansion
- `filters` (object|array, optional): Filterkriterien
  - Objektform: `{ "field": "value", ... }` → EQUALS-Filter (Whitelist-Prefilter für Vektor)
  - Arrayform: `[ {"field":"category","op":"EQUALS","value":"TEXT"} ]` (derzeit werden in Hybrid nur `EQUALS|EQ` unterstützt)
- `scoring` (object, optional): Gewichte für Fusion/Ranking in der Expansion
- `tie_break` (string, optional, Default `pk`): Tie-Break-Regel bei gleichen Scores (`pk` oder `none`)
- `tie_break_epsilon` (float, optional, Default 1e-12): Schwellwert, ab wann Scores als gleich gelten

Antwort (JSON):
```json
{
  "count": 3,
  "results": [
    { "pk": "chunk_001", "score": 0.9123 },
    { "pk": "chunk_017", "score": 0.9044 },
    { "pk": "chunk_083", "score": 0.8750 }
  ]
}
```

### Beispiele: Filter (Array-Form) mit IN und RANGE

Schema-Mapping (optional) für benutzerdefinierte Felder (Konfiguration als JSON in `config:content_filter_schema`):
```json
{
  "field_map": {
    "dataset": "user_metadata.dataset",
    "score":   "user_metadata.score"
  }
}
```

Request-Beispiel mit IN- und RANGE-Filtern (Array-Form):
```json
{
  "query": "any",
  "k": 10,
  "expand": { "hops": 0 },
  "filters": [
    {"field": "dataset", "op": "IN",    "values": ["train", "test"]},
    {"field": "score",   "op": "RANGE", "min": 0.5, "max": 1.0}
  ],
  "tie_break": "pk",
  "tie_break_epsilon": 1e-12
}
```

Hinweise zu Filtern in Hybrid:
- Objekt-Form (`{"field":"value"}`) entspricht EQUALS.
- Array-Form unterstützt derzeit `EQUALS|EQ`, `IN` (Werte-Array), `RANGE` (Objekt `{min,max}`) für gemappte Felder.
- Das Schema-Mapping ist nötig, um logische Felder (z. B. `dataset`) auf tatsächliche JSON-Pfade in den Content-Metadaten zu verweisen (z. B. `user_metadata.dataset`).

## POST /search/fusion

Fusion von Volltext- und Vektorsuche mit zwei Modi:
- `rrf` (Reciprocal Rank Fusion)
- `weighted` (gewichtete Scores mit Normalisierung)

Anfrage (JSON):
```json
{
  "table": "documents",
  "k": 10,
  "fusion_mode": "rrf",
  "text_column": "body",
  "text_query": "machine learning",
  "text_limit": 1000,
  "vector_query": [0.12, -0.03, 0.55],
  "vector_limit": 1000,
  "k_rrf": 60,
  "weight_text": 0.5,
  "alpha": 0.5,
  "min_text_score": 8.0,
  "max_vector_distance": 0.8,
  "tie_break": "pk",
  "tie_break_epsilon": 1e-9
}
```

Felder:
- `table` (string, erforderlich): Zieltabelle
- `k` (int, optional, Default 10): Anzahl finaler Ergebnisse
- `fusion_mode` (string, optional, Default `rrf`): `rrf` oder `weighted`
- `text_column` (string, optional) + `text_query` (string, optional): Aktivieren Volltextsuche; `text_limit` begrenzt BM25-Treffer
- `vector_query` (array<float>, optional): Aktiviert Vektorsuche; `vector_limit` begrenzt Vektor-Treffer
- `k_rrf` (int, optional, Default 60): RRF-Konstante für `rrf`
- `weight_text` (float, optional, Default 0.5): Gewicht der Textkomponente für `weighted` (0.0–1.0)
- `alpha` (Alias für `weight_text`): Wird bevorzugt, wenn vorhanden
- `min_text_score` (float, optional): Untergrenze für BM25-Scores (Filter vor Fusion)
- `max_vector_distance` (float, optional): Obergrenze für Vektordistanz (Filter vor Fusion)
- `tie_break` (string, optional, Default `pk`): Tie-Break-Regel bei gleichen Scores (`pk` oder `none`)
- `tie_break_epsilon` (float, optional, Default 1e-12): Schwellwert, ab wann Scores als gleich gelten
- `filters` (object oder array, optional): Attributfilter für Whitelist-Prefilter
  - Objektform: `{ "field": "value", ... }` → EQUALS-Filter
  - Arrayform: `[{"field":"category","op":"EQUALS","value":"TEXT"}]`, unterstützt `EQUALS|NOT_EQUALS|CONTAINS|IN|RANGE|min/max|GT|GTE|LT|LTE`

Antwort (JSON):
```json
{
  "count": 5,
  "fusion_mode": "rrf",
  "table": "documents",
  "results": [
    { "pk": "doc_42", "score": 0.0163 },
    { "pk": "doc_7",  "score": 0.0149 }
  ],
  "text_count": 1000,
  "vector_count": 1000
}
```

Hinweise:
- Mindestens eine der Komponenten (Text oder Vektor) muss vorhanden sein.
- Für `weighted` werden BM25-Scores und Vektordistanzen intern normalisiert; Distanzen werden zu Similaritäten konvertiert.
- Bei `rrf` wird ausschließlich die Rangposition verwendet; `k_rrf` steuert die Glättung.
- Tie-Break: Standardmäßig `pk` aufsteigend für deterministische Ergebnisse; kann deaktiviert werden (`tie_break=none`).
- Filters: Werden als Whitelist-Prefilter für die Vektorsuche verwendet und als Post-Filter auf Texttreffer angewendet.

## Fehlercodes
- 400: Ungültige Parameter (z. B. fehlendes `table`, leere `vector_query`)
- 500: Interner Fehler (Index-/Speicherfehler)
