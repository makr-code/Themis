# Vector Hybrid Search

Datum: 19. Nov 2025
Status: EXPERIMENTAL (Windows/MSVC: einzelne Tests schlagen fehl; siehe Known Issues)

Hinweis (19. Nov 2025): Auf Windows (MSVC 19.44) liefern einige GTests für `executeFilteredVectorSearch` trotz erfolgreicher Pre‑Filter‑Whitelist aktuell 0 Ergebnisse. Details und Reproduktion: `docs/KNOWN_ISSUES.md`.

## Ziel
Kombinierte Nutzung von ANN Vektorsuche (HNSW / Brute Force) mit attributbasiertem Pre- **und** Post-Filtering zur Reduktion der Kandidatenmenge und präziser Ergebnisanpassung.

## Komponenten
- Pre-Filtering: `searchKnnPreFiltered()` generiert Whitelist über SecondaryIndexManager für EQUALS / RANGE / IN / Vergleichsoperatoren.
- Post-Filtering (Hybrid): `QueryEngine::executeFilteredVectorSearch()` wendet alle Operatoren (inkl. NOT_EQUALS, CONTAINS) auf geladene Entities nach Distanzsortierung an.

## Unterstützte Operatoren
| Operator | Pre-Filter | Post-Filter | Beschreibung |
|----------|-----------|-------------|--------------|
| EQUALS | ✅ | ✅ | exakte Übereinstimmung |
| NOT_EQUALS | ❌ (Scan nötig) | ✅ | Ausschluss von Wert |
| CONTAINS | ❌ (Substring) | ✅ | Teilstring in Textfeld |
| GREATER_THAN | ✅ (Range Scan) | ✅ | numerisch > |
| LESS_THAN | ✅ | ✅ | numerisch < |
| GREATER_EQUAL | ✅ | ✅ | numerisch >= |
| LESS_EQUAL | ✅ | ✅ | numerisch <= |
| IN | ✅ (Union von EQUALS) | ✅ | Wert in Menge |
| RANGE | ✅ | ✅ | min <= x <= max |

## Ablauf
1. Aufteilung der Filter: Pre-Filter geeignete Operatoren -> SecondaryIndex Scans.
2. Whitelist Intersection (AND Semantik).
3. ANN Suche (HNSW oder Fallback) mit Whitelist.
4. Laden der Entities (RocksDB) und Anwendung aller Filter inkl. NOT_EQUALS / CONTAINS.
5. Kürzung auf Top-k unter Beibehaltung Distanzsortierung.

## Performance Hinweise
- Selektive Filter zuerst verarbeiten um Whitelist früh zu schrumpfen.
- `max_filter_scan_size` begrenzt Range-Scans; Überschreitung -> Fallback auf Post-Filtering.
- NOT_EQUALS und CONTAINS immer Post-Filter (verhindert teure Vollscans vor ANN).
- Bei sehr großer Whitelist > Schwelle: Standard KNN + Post-Filter (verhindert riesige HNSW calls).

## Beispiele
```cpp
FilteredVectorSearchQuery q;
q.table = "documents";
q.query_vector = embedding;
q.k = 15;
// Pre-Filter Kandidaten schrumpfen
q.filters.push_back({"category", FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS, "tech"});
// Range Scan kombiniert
FilteredVectorSearchQuery::AttributeFilter scoreRange;
scoreRange.field = "score";
scoreRange.op = FilteredVectorSearchQuery::AttributeFilter::Op::RANGE;
scoreRange.value_min = "0.6";
scoreRange.value_max = "0.85";
q.filters.push_back(scoreRange);
// Post-Filter nur
FilteredVectorSearchQuery::AttributeFilter langContains;
langContains.field = "lang";
langContains.op = FilteredVectorSearchQuery::AttributeFilter::Op::CONTAINS;
langContains.value = "en"; // Teilstring (z.B. 'en-US')
q.filters.push_back(langContains);

auto [st, results] = engine.executeFilteredVectorSearch(q);
```

## Erweiterungen (geplant)
- ~~Radius-Suche (epsilon Nachbarn unter Distanzschwelle)~~ ✅ **IMPLEMENTIERT**
- Score Fusion (Vector Distanz + Attributgewichtung)
- Adaptive candidateMultiplier basierend auf selektiver Filterstatistik
 - Deterministische Tie-Breaks und Cutoffs (BM25 min_score, Vector max_distance) ✅

## Radius Search (Epsilon Neighbors)

**API**: `executeRadiusVectorSearch(RadiusVectorSearchQuery)`

**Verwendung**: Alle Vektoren innerhalb Distanzschwelle finden (statt Top-k).

**Parameter**:
- `epsilon`: Maximale Distanz (threshold)
- `max_results`: Optional Obergrenze (0 = unbegrenzt)
- `filters`: Attributfilter wie bei Filtered Search

**Beispiel**:
```cpp
RadiusVectorSearchQuery rq;
rq.table = "products";
rq.query_vector = userPreferenceEmbedding;
rq.epsilon = 0.3f;  // Nur sehr ähnliche Produkte
rq.max_results = 50; // Max 50 Ergebnisse

// Filter: nur verfügbare Produkte
FilteredVectorSearchQuery::AttributeFilter availFilter;
availFilter.field = "in_stock";
availFilter.op = FilteredVectorSearchQuery::AttributeFilter::Op::EQUALS;
availFilter.value = "true";
rq.filters.push_back(availFilter);

auto [st, results] = engine.executeRadiusVectorSearch(rq);
// Alle Produkte mit Distanz <= 0.3 und in_stock=true
```

**Performance**:
- HNSW: Fetch large k, filter by epsilon (keine native radius support)
- Brute-Force: Direkter Distanzcheck während Scan
- Pre-Filter reduziert Suchraum deutlich bei selektiven Attributen

**Anwendungsfälle**:
- Clustering (alle Nachbarn in Radius)
- Deduplizierung (Duplikate unter Schwelle)
- Anomalie-Detektion (isolierte Punkte mit wenigen epsilon-Nachbarn)

## Fehlende Teile
- Radius Search API
- Distanz-Re-Ranking mit Attributgewichten
- Erweiterte Metriken (DOT kombiniert mit Normierung)

## Tests
- Vorhandene `test_filtered_vector_search.cpp` deckt EQUALS / RANGE / IN / Kombi ab.
- Zusätzliche Tests für NOT_EQUALS & CONTAINS werden nach Test-Suite Reparatur ergänzt.
 - Fusion-Tests: RRF vs. Weighted mit Tie-Break (`pk`) und Cutoffs (`min_text_score`, `max_vector_distance`); deterministische Reihenfolge bei Gleichstand.

## HTTP API Notizen (Hybrid & Fusion)

Dieser Abschnitt fasst die wichtigsten HTTP-Parameter und das Zusammenwirken von Pre- und Post-Filtering zusammen. Details und Beispiele siehe `docs/apis/hybrid_search_api.md`.

- Hybrid `/search/hybrid` (Content + optional Graph-Expansion)
	- `filters`: Objekt- oder Array-Form
		- Objekt: `{ "field": "value" }` → EQUALS (Whitelist-Prefilter)
		- Array: `[ {"field":"dataset","op":"IN","values":["train","test"]}, {"field":"score","op":"RANGE","min":0.5,"max":1.0} ]`
			- Unterstützt in Hybrid derzeit: `EQUALS|EQ`, `IN`, `RANGE` (über Schema-Mapping `field_map` auf Content-JSON-Pfade)
	- `tie_break` (`pk|none`) + `tie_break_epsilon`: deterministische Sortierung bei quasi gleichen Scores
	- Pre-Filter reduziert Vektor-Kandidaten via Whitelist; Post-Filter (Entity-Load) für komplexe Operatoren bleibt im Core erhalten.

- Fusion `/search/fusion` (Text + Vektor)
	- Modi: `rrf` (Rangfusion) und `weighted` (gewichtete Normalisierung)
	- Alias: `alpha` entspricht `weight_text` (Gewicht der Textkomponente)
	- Cutoffs: `min_text_score` filtert BM25 vor Fusion; `max_vector_distance` filtert Vektortreffer vor Fusion
	- `filters`: Whitelist-Prefilter für Vektor, Post-Filter für Text (Attribute werden auf Entities geprüft)
	- Tie-Break: `tie_break` + `tie_break_epsilon` für stabile Reihenfolge bei gleichen Fusionsscores

Minimalbeispiel (Hybrid mit IN/RANGE und deterministischem Tie-Break):

```json
{
	"query": "any",
	"k": 10,
	"expand": { "hops": 0 },
	"filters": [
		{"field": "dataset", "op": "IN", "values": ["train", "test"]},
		{"field": "score",   "op": "RANGE", "min": 0.5, "max": 1.0}
	],
	"tie_break": "pk",
	"tie_break_epsilon": 1e-12
}
```

## Wartung
Siehe Roadmap Wartungsaufgabe "Test Suite Reparatur" für Anpassung legacy Tests nach Hybrid-Erweiterung.
