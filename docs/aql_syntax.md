# AQL - THEMIS Query Language

**Version:** 1.0  
**Datum:** 30. Oktober 2025  
**Inspiriert von:** ArangoDB AQL, mit Fokus auf Multi-Modell-Queries

---

## �berblick

**AQL (Advanced Query Language)** ist eine deklarative SQL-�hnliche Sprache f�r THEMIS, optimiert f�r hybride Queries �ber relationale, Graph-, Vektor- und Dokument-Daten.

**Design-Prinzipien:**
- ? **Einfach:** SQL-�hnliche Syntax f�r schnelle Adoption
- ? **M�chtig:** Multi-Modell-Support (Relational, Graph, Vector)
- ? **Optimierbar:** Automatische Index-Auswahl via Optimizer
- ? **Erweiterbar:** Schrittweise Erweiterung (Aggregationen, Joins, Subqueries)

---

## Syntax-�bersicht

### Grundstruktur

```aql
FOR variable IN collection
  [LET var = expression [, ...]]
  [FILTER condition]
  [SORT expression [ASC|DESC] [, ...]]
  [LIMIT offset, count]
  [RETURN expression]
```

**Execution-Reihenfolge:**
1. `FOR` - Iteration �ber Collection/Index
2. `FILTER` - Pr�dikat-Evaluation (mit Index-Nutzung)
3. `SORT` - Sortierung (mit Index-Nutzung wenn m�glich)
4. `LIMIT` - Pagination/Offset
5. `RETURN` - Projektion (Felder/Objekte/Arrays)

---

## MVP-Einschr�nkungen und Hinweise

Damit Erwartungen klar sind, hier die wichtigsten Begrenzungen des aktuellen MVP:

- OR-Operator: Vollst�ndig unterst�tzt �ber DNF-Konvertierung. FULLTEXT kann in OR-Ausdr�cken verwendet werden.
- Feld-zu-Feld Vergleiche (z. B. `u.city == o.city`) sind im Translator nicht allgemein erlaubt. Ein spezieller Join-Pfad erlaubt jedoch Gleichheits-Joins �ber genau zwei FOR-Klauseln (siehe Abschnitt �Einfache Joins (MVP)�).
- LET in FILTER: Falls einfache LET-Bindungen in FILTER vorkommen, werden diese vor der �bersetzung extrahiert (�pre-extracted�). Bei `explain: true` signalisiert der Plan dies mit `plan.let_pre_extracted = true`.
- Subqueries, OR, komplexe Ausdr�cke/Funktionen sind (noch) eingeschr�nkt und werden iterativ erweitert.

## Kern-Klauseln

### 1. FOR - Collection Iteration

```aql
FOR doc IN users
  RETURN doc

FOR u IN users
  FILTER u.age > 18
  RETURN u.name
```

**Syntax:**
- `variable` - Beliebiger Bezeichner (lowercase empfohlen)
- `collection` - Table-Name aus Storage-Layer

**Multi-Collection (Joins - MVP seit 31.10.2025):**

Themis unterst�tzt Nested-Loop-Joins �ber mehrere Collections via sequenzielle `FOR`-Klauseln:

```aql
FOR u IN users
  FOR o IN orders
    FILTER o.user_id == u._key
    RETURN {user: u.name, order: o.id}
```

**Join-Arten (MVP):**
- **Equality Join:** Verkn�pfung �ber `FILTER var1.field == var2.field`
- **Cross Product + Filter:** Kartesisches Produkt mit nachtr�glicher Filterung

**Beispiel - User-City-Join:**
```aql
FOR user IN users
  FOR city IN cities
    FILTER user.city_id == city._key
    RETURN {
      user_name: user.name,
      city_name: city.name,
      country: city.country
    }
```

**Performance-Hinweise:**
- ?? Nested-Loop kann **teuer** sein bei gro�en Datasets (O(n�m) Komplexit�t)
- ?? Empfehlung: FILTER-Bedingungen so spezifisch wie m�glich
- ?? Zuk�nftig: Hash-Join-Optimierung f�r gro�e Collections geplant
- ?? Verwende Indizes auf Join-Spalten (z.B. `city_id`) wo m�glich

**Multi-FOR Limitierungen (MVP):**
- Maximal 2-3 FOR-Klauseln empfohlen (Performance)
- Join-Bedingung muss in FILTER sein (keine impliziten Joins)
- Nur Equality-Joins (`==`) optimiert

---

### 2. FILTER - Bedingungen

**Vergleichsoperatoren:**
```aql
FILTER doc.age == 25          // Gleichheit
FILTER doc.age != 25          // Ungleichheit
FILTER doc.age > 18           // Gr��er
FILTER doc.age >= 18          // Gr��er-Gleich
FILTER doc.age < 65           // Kleiner
FILTER doc.age <= 65          // Kleiner-Gleich
```

**Logische Operatoren:**
```aql
FILTER doc.age > 18 AND doc.city == "Berlin"
FILTER doc.status == "active" OR doc.status == "pending"
FILTER NOT doc.deleted
```

**OR-Operator (v1.3):**
```aql
// Einfaches OR
FILTER doc.status == "active" OR doc.status == "pending"

// OR mit AND kombiniert
FILTER (doc.status == "active" AND doc.age >= 30) OR doc.city == "Berlin"

// Komplexe DNF-Expansion
FILTER (doc.city == "Berlin" OR doc.city == "Munich") AND doc.status == "active"
```

**IN-Operator:**
```aql
FILTER doc.status IN ["active", "pending", "approved"]
FILTER doc.age IN [18, 21, 25, 30]
```

**String-Operatoren:**
```aql
FILTER LIKE(doc.name, "Max%")           // Prefix-Match
FILTER CONTAINS(doc.description, "AI")  // Substring
FILTER REGEX_TEST(doc.email, ".*@example\.com")
```

**Fulltext-Suche (BM25-ranked):**
```aql
FILTER FULLTEXT(doc.content, "machine learning")              // Multi-term search
FILTER FULLTEXT(doc.title, '"exact phrase"')                  // Phrase search (escaped quotes)
FILTER FULLTEXT(doc.abstract, "neural networks", 50)          // Custom limit (default: 1000)

// **NEU v1.3:** FULLTEXT + AND Kombinationen (Hybrid Search)
FILTER FULLTEXT(doc.content, "AI") AND doc.year >= 2023
FILTER FULLTEXT(doc.title, "neural") AND doc.category == "Research" AND doc.views >= 1000
FILTER doc.lang == "en" AND FULLTEXT(doc.abstract, "machine learning")  // Order flexible
```

**FULLTEXT-Funktionsdetails:**
- **Argumente:** `FULLTEXT(field, query [, limit])`
  - `field` - Spaltenname mit Fulltext-Index
  - `query` - Suchquery (Tokens mit AND-Logik, oder `"phrase"` f�r exakte Phrasen)
  - `limit` - Optional: Max. Ergebnisse (default 1000)
- **Ranking:** BM25-Scoring (k1=1.2, b=0.75)
- **Features:** Stemming (EN/DE), Stopwords, Normalization (Umlaute)
- **Hybrid Queries (v1.3):** 
  - ? `FULLTEXT(...) AND <predicates>` - Intersection-based (BM25 n structural filters)
  - ? `FULLTEXT(...) OR <expr>` - Noch nicht unterst�tzt (geplant v1.4)
- **Execution Strategy:** Fulltext-Scan zuerst (BM25-ranked), dann Intersection mit strukturellen Filtern
- **Siehe:** `docs/search/fulltext_api.md` f�r Index-Erstellung und Konfiguration

**NULL-Checks:**
```aql
FILTER doc.email != null
FILTER doc.phone == null
```

---

### 3. SORT - Sortierung

**Einfache Sortierung:**
```aql
SORT doc.age                  // ASC (default)
SORT doc.age DESC
SORT doc.created_at DESC
```

**Multi-Column-Sort:**
```aql
SORT doc.city ASC, doc.age DESC
SORT doc.priority DESC, doc.created_at ASC
```

**Index-Nutzung:**
- Range-Index auf `age` ? effiziente Sortierung
- Composite-Index `(city, age)` ? optimale Multi-Column-Sort

---

### 4. LIMIT - Pagination

**Syntax:**
```aql
LIMIT count                   // Erste N Ergebnisse
LIMIT offset, count           // Pagination
```

**Beispiele:**
```aql
LIMIT 10                      // Erste 10
LIMIT 20, 10                  // Zeilen 21-30 (Seite 3)
```

**Best Practices:**
- Immer mit `LIMIT` arbeiten (verhindert Full-Scans)
- F�r gro�e Offsets: Cursor-basierte Pagination bevorzugen

---

### 5. RETURN - Projektion

**Ganzes Dokument:**
```aql
RETURN doc
```

**Einzelne Felder:**
```aql
RETURN doc.name
RETURN doc.email
```

**Objekt-Konstruktion:**
```aql
RETURN {
  name: doc.name,
  age: doc.age,
  city: doc.city
}
```

**Berechnete Felder:**
```aql
RETURN {
  name: doc.name,
  age_in_months: doc.age * 12,
  full_address: CONCAT(doc.street, ", ", doc.city)
}
```

**Arrays:**
```aql
RETURN [doc.name, doc.age, doc.city]
```

Unterst�tzte Ausdr�cke im MVP:
- Literale: Zahl, String, Bool, null
- Variablen und Feldzugriff: `doc`, `doc.field`
- Objekt- und Array-Literale (verschachtelt m�glich)
- Einfache Let-Bindings pro Zeile (siehe LET)

---

## Erweiterte Features (Phase 1.1+)

### LET - Variable Binding (MVP seit 31.10.2025)

Bindet pro Iteration Werte an Variablen, die in `FILTER` und `RETURN` genutzt werden k�nnen.

**Einfaches Beispiel:**
```aql
FOR u IN users
  LET city_name = u.city
  RETURN {name: u.name, city: city_name}
```

**Berechnungen mit LET:**
```aql
FOR product IN products
  LET total_value = product.price * product.quantity
  FILTER total_value > 1000
  RETURN {
    product: product.name,
    value: total_value
  }
```

**Mehrere LET-Bindungen:**
```aql
FOR sale IN sales
  LET net = sale.amount
  LET tax = net * 0.19
  LET gross = net + tax
  RETURN {sale_id: sale._key, net, tax, gross}
```

**LET in Joins:**
```aql
FOR user IN users
  FOR order IN orders
    FILTER order.user_id == user._key
    LET full_name = CONCAT(user.first_name, " ", user.last_name)
    RETURN {customer: full_name, order_id: order._key}
```

**MVP-Einschr�nkungen:**
- Unterst�tzt sind aktuell einfache Ausdr�cke: Literale, Variablen, Feldzugriffe, Bin�roperationen (+, -, *, /), Objekt-/Array-Literale
- LETs werden sequenziell ausgewertet; sp�tere LETs k�nnen fr�here verwenden
- Komplexe Funktionen (CONCAT, SUBSTRING, etc.) in Entwicklung
- Explain: Wenn `LET`-Variablen in `FILTER` zu einfachen Gleichheitspr�dikaten vor der �bersetzung extrahiert wurden, enth�lt der Plan das Flag `plan.let_pre_extracted = true`

---

### COLLECT - Aggregationen (MVP seit 31.10.2025)

Gruppiert Ergebnisse und berechnet Aggregatfunktionen.

**Einfaches GROUP BY:**
```aql
FOR user IN users
  COLLECT city = user.city
  RETURN {city, count: LENGTH(1)}
```

**COUNT-Aggregation:**
```aql
FOR user IN users
  COLLECT city = user.city WITH COUNT INTO total
  RETURN {city, total}
```

**SUM-Aggregation:**
```aql
FOR sale IN sales
  COLLECT category = sale.category
  AGGREGATE total_revenue = SUM(sale.amount)
  RETURN {category, total_revenue}
```

**Mehrere Aggregationen:**
```aql
FOR order IN orders
  COLLECT status = order.status
  AGGREGATE 
    total_count = COUNT(),
    total_amount = SUM(order.amount),
    avg_amount = AVG(order.amount),
    min_amount = MIN(order.amount),
    max_amount = MAX(order.amount)
  RETURN {status, total_count, total_amount, avg_amount, min_amount, max_amount}
```

**COLLECT mit FILTER:**
```aql
FOR user IN users
  FILTER user.age > 18
  COLLECT city = user.city
  AGGREGATE adult_count = COUNT()
  RETURN {city, adult_count}
```

**Unterst�tzte Aggregatfunktionen (MVP):**
- `COUNT()` - Anzahl der Gruppen-Elemente
- `SUM(expr)` - Summe eines numerischen Felds
- `AVG(expr)` - Durchschnitt eines numerischen Felds
- `MIN(expr)` - Minimum eines Felds
- `MAX(expr)` - Maximum eines Felds

**Performance-Hinweise:**
- Hash-basiertes Grouping: O(n) Komplexit�t
- FILTER vor COLLECT reduziert Datenvolumen (wird automatisch optimiert)
- F�r sehr gro�e Gruppen: Memory-Nutzung beachten

**Geplante Erweiterungen:**
- `STDDEV(expr)` - Standardabweichung
- `VARIANCE(expr)` - Varianz
- `PERCENTILE(expr, n)` - n-tes Perzentil
- `UNIQUE(expr)` - Distinct Values

Hinweise (MVP):
- Gruppierung erfolgt �ber exakte String-Matches der Group-Keys
- Mehrere GROUP BY-Felder via Tuple-Keys geplant
- HAVING-Clause (Post-Aggregation-Filter) in Entwicklung

---

## HTTP-spezifische Parameter f�r Pagination

Bei Nutzung des HTTP-Endpunkts `POST /query/aql` k�nnen optionale Felder zur Pagination mitgegeben werden:

```json
{
  "query": "FOR u IN users SORT u.age ASC LIMIT 10 RETURN u",
  "use_cursor": true,
  "cursor": "<token-aus-previous-response>",
  "allow_full_scan": false
}
```

- `use_cursor` (bool): Aktiviert Cursor-basierte Pagination. Antwortformat enth�lt `{items, has_more, next_cursor, batch_size}`.
- `cursor` (string): Token aus `next_cursor` der vorherigen Seite. G�ltig nur in Kombination mit `use_cursor: true`.
- `allow_full_scan` (bool): Optionaler Fallback f�r kleine Datenmengen/Tests; f�r gro�e Daten wird Index-basierte Sortierung empfohlen.

Weitere Details siehe `docs/cursor_pagination.md`.

---

## Spezial-Queries

### Graph-Traversierung

```aql
FOR v, e, p IN 1..3 OUTBOUND "users/alice" edges
  FILTER v.active == true
  RETURN {vertex: v, edge: e, path: p}
```

**Traversal-Richtungen:**
- `OUTBOUND` - Ausgehende Kanten (Alice ? Bob)
- `INBOUND` - Eingehende Kanten (Alice ? Bob)
- `ANY` - Beide Richtungen

**Depth-Limits:**
- `1..1` - Nur direkte Nachbarn
- `1..3` - Bis zu 3 Hops
- `2..5` - Min 2, Max 5 Hops

---

### Vektor-�hnlichkeitssuche

```aql
FOR doc IN users
  NEAR(doc.embedding, @query_vector, 10)
  FILTER doc.age > 18
  RETURN {name: doc.name, similarity: SIMILARITY()}
```

**Funktionen:**
- `NEAR(field, vector, k)` - k-NN-Suche
- `SIMILARITY()` - Aktueller Similarity-Score (0.0 - 1.0)

**Metriken:**
```aql
NEAR(doc.embedding, @query_vector, 10, "cosine")    // Cosine Similarity
NEAR(doc.embedding, @query_vector, 10, "euclidean") // L2-Distance
```

---

### Geo-Queries

```aql
FOR doc IN locations
  GEO_DISTANCE(doc.lat, doc.lon, 52.52, 13.405) < 5000
  RETURN {name: doc.name, distance: GEO_DISTANCE(doc.lat, doc.lon, 52.52, 13.405)}
```

**Funktionen:**
- `GEO_DISTANCE(lat1, lon1, lat2, lon2)` - Haversine-Distanz (Meter)
- `GEO_BOX(lat, lon, minLat, maxLat, minLon, maxLon)` - Bounding-Box-Check

---

### Fulltext-Suche (BM25)

**Einfache Multi-Term-Suche:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "machine learning")
  LIMIT 10
  RETURN {title: doc.title, content: doc.content}
```

**Sortierung nach Score (BM25):**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "neural networks")
  SORT BM25(doc) DESC
  LIMIT 10
  RETURN {title: doc.title, score: BM25(doc)}
```

**Phrasensuche:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.abstract, '"neural networks"')
  LIMIT 20
  RETURN doc
```

**Mit benutzerdefiniertem Limit:**
```aql
FOR doc IN research_papers
  FILTER FULLTEXT(doc.content, "deep learning transformer", 50)
  RETURN {
    title: doc.title,
    authors: doc.authors,
    year: doc.year
  }
```

**Volltext + strukturierte Filter kombiniert:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "AI") AND doc.year >= 2023
  LIMIT 10
  RETURN doc
```

**Volltext + OR-Kombinationen:**
```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "machine learning") OR doc.year < 2000
  LIMIT 10
  RETURN {title: doc.title, year: doc.year}
```

**Hinweise:**
- BM25-Ranking: Ergebnisse sind automatisch nach Relevanz sortiert (höchster Score zuerst)
- Score aus AQL zugreifbar: `BM25(doc)` liefert den Score für das aktuelle Dokument
- Index erforderlich: `POST /api/index/fulltext` (siehe `docs/search/fulltext_api.md`)
- Stemming/Stopwords/Normalisierung: Per Index konfigurierbar (EN/DE)
- Score-Ausgabe: Verf�gbar in RETURN via `FULLTEXT_SCORE()` (nur wenn ein `FULLTEXT(...)`-Filter in der Query vorhanden ist)
- AND/OR-Kombinationen: `FULLTEXT(...) AND ...` und `FULLTEXT(...) OR ...` vollständig produktiv

**Index-Erstellung (HTTP API):**
```json
POST /api/index/fulltext
{
  "table": "articles",
  "column": "content",
  "stemming_enabled": true,
  "language": "en",
  "stopwords_enabled": true,
  "normalize_german": false
}
```

---

### Fulltext-Suche

```aql
FOR doc IN articles
  FILTER FULLTEXT(doc.content, "machine learning AI")
  LIMIT 10
  RETURN {title: doc.title, score: FULLTEXT_SCORE()}
```

**Funktionen:**
- `FULLTEXT(field, query [, limit])` - Tokenisierte Suche mit optionalem Limit (Kandidatenzahl)
- `FULLTEXT_SCORE()` - Relevanz-Score (BM25) des aktuellen Treffers; nur g�ltig, wenn ein `FULLTEXT(...)`-Filter vorhanden ist

---

## Einfache Joins (MVP)

Unterst�tzt werden Equality-Joins �ber genau zwei `FOR`-Klauseln mit einem Gleichheitspr�dikat zwischen Variablen.

```aql
FOR u IN users
  FOR o IN orders
  FILTER u._key == o.user_id
  RETURN u
```

Eigenschaften und Einschr�nkungen (MVP):
- Genau zwei `FOR`-Klauseln; ein Equality-Pr�dikat `var1.field == var2.field` in `FILTER`.
- Zus�tzliche `FILTER` pro Seite sind erlaubt und werden vor dem Join angewendet.
- `RETURN` muss aktuell eine der Variablen zur�ckgeben (typisch `u` oder `o`).
- `LIMIT` wird nach dem Join angewendet. `SORT` im Join-Pfad ist derzeit nicht unterst�tzt.
- `explain: true` liefert einen Plan, der den Join-Pfad ausweist; bei LET-Pre-Extraction wird `plan.let_pre_extracted = true` gesetzt.

Projektion mit LET im Join-Kontext:

```aql
FOR u IN users
  FOR o IN orders
  FILTER u._key == o.user_id
  LET info = { user: u.name, order: o.id }
  RETURN info
```

Hinweis: Komplexe Projektionen k�nnen je nach Datenvolumen h�here Kosten verursachen; nutze `LIMIT` wo sinnvoll.

---

## Funktionen & Operatoren

### String-Funktionen

```aql
CONCAT(str1, str2, ...)       // "Hello" + " " + "World"
LOWER(str)                     // "HELLO" ? "hello"
UPPER(str)                     // "hello" ? "HELLO"
SUBSTRING(str, offset, length) // "Hello"[1:4] ? "ell"
LENGTH(str)                    // "Hello" ? 5
TRIM(str)                      // "  Hello  " ? "Hello"
```

### Numeric-Funktionen

```aql
ABS(num)                       // |-5| ? 5
CEIL(num) / FLOOR(num)         // 3.7 ? 4 / 3
ROUND(num, decimals)           // 3.14159, 2 ? 3.14
SQRT(num)                      // v16 ? 4
POW(base, exp)                 // 2^8 ? 256
```

### Aggregations (in COLLECT)

```aql
COUNT()                        // Anzahl Zeilen
SUM(expr)                      // Summe
AVG(expr)                      // Durchschnitt
MIN(expr) / MAX(expr)          // Minimum/Maximum
STDDEV(expr)                   // Standardabweichung
VARIANCE(expr)                 // Varianz
```

### Type-Checks

```aql
IS_NULL(value)
IS_NUMBER(value)
IS_STRING(value)
IS_ARRAY(value)
IS_OBJECT(value)
```

---

## Beispiel-Queries

### 1. Einfache Filterung

```aql
FOR user IN users
  FILTER user.age > 18 AND user.city == "Berlin"
  SORT user.created_at DESC
  LIMIT 10
  RETURN {
    name: user.name,
    email: user.email,
    age: user.age
  }
```

**Optimizer:**
- Nutzt Composite-Index `(city, age)` falls vorhanden
- Fallback: Equality-Index `city` + Full-Scan-Filter `age`

---

### 2. Geo-Proximity-Search

```aql
FOR loc IN restaurants
  FILTER GEO_DISTANCE(loc.lat, loc.lon, 52.52, 13.405) < 2000
  FILTER loc.rating >= 4.0
  SORT GEO_DISTANCE(loc.lat, loc.lon, 52.52, 13.405) ASC
  LIMIT 5
  RETURN {
    name: loc.name,
    rating: loc.rating,
    distance: GEO_DISTANCE(loc.lat, loc.lon, 52.52, 13.405)
  }
```

**Optimizer:**
- Nutzt Geo-Index f�r Bounding-Box-Scan
- Post-Filter f�r exakte Distanz-Berechnung

---

### 3. Vektor-Suche mit Filter

```aql
FOR product IN products
  NEAR(product.embedding, @query_vector, 20)
  FILTER product.price < 100.0 AND product.in_stock == true
  SORT SIMILARITY() DESC
  LIMIT 10
  RETURN {
    name: product.name,
    price: product.price,
    similarity: SIMILARITY()
  }
```

**Pre-Filtering vs Post-Filtering:**
- Pre-Filter: Bitset f�r `price < 100 AND in_stock == true` ? k-NN
- Post-Filter: k-NN (20) ? Filter ? Top-10

---

### 4. Aggregationen

```aql
FOR order IN orders
  FILTER order.created_at >= "2025-01-01"
  COLLECT city = order.city
  AGGREGATE 
    total_revenue = SUM(order.amount),
    avg_order = AVG(order.amount),
    order_count = COUNT()
  SORT total_revenue DESC
  LIMIT 10
  RETURN {
    city,
    total_revenue,
    avg_order,
    order_count
  }
```

---

### 5. Graph-Traversierung

```aql
FOR vertex, edge, path IN 1..3 OUTBOUND "users/alice" friendships
  FILTER vertex.active == true
  RETURN {
    friend: vertex.name,
    connection_type: edge.type,
    path_length: LENGTH(path.edges)
  }
```

---

## Query-Execution & Optimizer

### Explain-Plan

```json
POST /query/aql
{
  "query": "FOR u IN users FILTER u.age > 18 SORT u.created_at DESC LIMIT 10",
  "explain": true
}
```

**Response:**
```json
{
  "plan": {
    "mode": "range_aware",
    "order": [
      { "column": "created_at", "value": "DESC" }
    ],
    "estimates": [
      { "column": "age", "value": "> 18", "estimatedCount": 1200, "capped": false }
    ],
    "let_pre_extracted": true
  }
}
```

### Index-Hints (sp�ter)

```aql
FOR doc IN users USE INDEX idx_age_city
  FILTER doc.age > 18
  RETURN doc
```

---

## AST-Struktur (Internal)

```cpp
// AST-Node-Typen
enum class ASTNodeType {
    ForNode,          // FOR variable IN collection
    FilterNode,       // FILTER condition
    SortNode,         // SORT expr [ASC|DESC]
    LimitNode,        // LIMIT offset, count
    ReturnNode,       // RETURN expression
    
    // Expressions
    BinaryOp,         // ==, !=, >, <, >=, <=, AND, OR
    UnaryOp,          // NOT, -
    FunctionCall,     // CONCAT, SUM, etc.
    FieldAccess,      // doc.field
    Literal,          // "string", 123, true, null
    Variable          // doc, user, etc.
};

// Beispiel-AST f�r: FOR u IN users FILTER u.age > 18 RETURN u.name
ForNode {
    variable: "u",
    collection: "users",
    
    filter: FilterNode {
        condition: BinaryOp {
            op: ">",
            left: FieldAccess("u", "age"),
            right: Literal(18)
        }
    },
    
    return_expr: ReturnNode {
        expression: FieldAccess("u", "name")
    }
}
```

---

## Implementierungs-Phasen

### Phase 1 (MVP - Woche 1-2):
- ? FOR, FILTER (Equality, Range, IN), SORT, LIMIT, RETURN
- ? Parser (PEGTL)
- ? AST ? QueryEngine-Translation
- ? HTTP-Endpoint `/query/aql`
- ? Unit-Tests

### Phase 2 (Woche 3-4):
- LET (Variable Binding)
- COLLECT (Aggregationen: COUNT, SUM, AVG)
- String-/Numeric-Funktionen
- Explain-Plan-Integration

### Phase 3 (Woche 5-6):
- Graph-Traversierung (FOR v, e, p IN ... OUTBOUND)
- Vektor-Suche (NEAR, SIMILARITY)
- Geo-Queries (GEO_DISTANCE, GEO_BOX)
- Fulltext (FULLTEXT, BM25)

### Phase 4 (sp�ter):
- Joins (Multi-Collection)
- Subqueries
- Transactions (BEGIN, COMMIT, ROLLBACK)
- INSERT, UPDATE, DELETE via AQL

---

## Performance-�berlegungen

**Index-Nutzung:**
- FILTER mit `==` ? Equality-Index
- FILTER mit `>`, `<` ? Range-Index
- FILTER mit `IN` ? Batch-Lookup
- SORT ? Range-Index (wenn vorhanden)

**Optimizer-Strategien:**
- **Filter-Pushdown:** FILTER vor SORT (reduziert Sortier-Kosten)
- **Index-Auswahl:** Kleinster gesch�tzter Index zuerst
- **Short-Circuit:** LIMIT fr�h anwenden (z.B. Top-K)

**Vermeiden:**
- Full-Table-Scans ohne LIMIT
- Sortierung ohne Index auf gro�en Datasets
- Aggregationen ohne COLLECT (ineffizient)

---

## Kompatibilit�t & Erweiterungen

**ArangoDB AQL:**
- �hnliche Syntax (FOR, FILTER, SORT, LIMIT, RETURN)
- Unterschiede: THEMIS nutzt natives MVCC, kein `_key` zwingend

**SQL-Vergleich:**
```sql
-- SQL
SELECT name, age FROM users WHERE age > 18 ORDER BY created_at DESC LIMIT 10;

-- AQL
FOR user IN users
  FILTER user.age > 18
  SORT user.created_at DESC
  LIMIT 10
  RETURN {name: user.name, age: user.age}
```

**Vorteile AQL:**
- Multi-Modell (Graph, Vector, Geo in einer Query)
- Explizite Execution-Reihenfolge (leichter zu optimieren)
- Schemalos (flexible Felder)

---

## Fehlerbehandlung

**Syntax-Errors:**
```json
{
  "error": "Syntax error at line 2, column 10: Expected 'IN' after variable name",
  "query": "FOR user users FILTER ...",
  "line": 2,
  "column": 10
}
```

**Semantic-Errors:**
```json
{
  "error": "Collection 'userz' does not exist (did you mean 'users'?)",
  "query": "FOR u IN userz RETURN u"
}
```

**Runtime-Errors:**
```json
{
  "error": "Division by zero in expression: amount / quantity",
  "entity_key": "orders:12345"
}
```

---

## Referenz-Links

- **Parser:** PEGTL (https://github.com/taocpp/PEGTL)
- **Inspiration:** ArangoDB AQL (https://www.arangodb.com/docs/stable/aql/)
- **Optimizer:** docs/query_optimizer.md
- **Index-Typen:** docs/indexes.md

---

**Status:** ? Syntax-Definition vollst�ndig  
**N�chster Schritt:** Parser-Implementation mit PEGTL

## Vollst�ndige Beispiele (MVP Features)

### Beispiel 1: User-City-Join mit Aggregation

**Szenario:** Finde alle User in ihren St�dten, gruppiert nach Land mit Z�hlung:

```aql
FOR user IN users
  FOR city IN cities
    FILTER user.city_id == city._key
    COLLECT country = city.country
    AGGREGATE user_count = COUNT()
    RETURN {country, user_count}
```

**Ergebnis:**
```json
[
  {"country": "Germany", "user_count": 125},
  {"country": "France", "user_count": 87},
  {"country": "Spain", "user_count": 43}
]
```

---

### Beispiel 2: Sales-Analyse mit LET und Aggregation

**Szenario:** Berechne Netto/Brutto-Ums�tze pro Kategorie:

```aql
FOR sale IN sales
  LET net = sale.amount
  LET tax = net * 0.19
  LET gross = net + tax
  COLLECT category = sale.category
  AGGREGATE 
    total_net = SUM(net),
    total_gross = SUM(gross),
    count = COUNT()
  RETURN {
    category,
    total_net,
    total_gross,
    avg_sale: total_net / count,
    count
  }
```

---

### Beispiel 3: Top-10 St�dte nach User-Count

**Szenario:** H�ufigste St�dte finden:

```aql
FOR user IN users
  COLLECT city_id = user.city_id WITH COUNT INTO user_count
  SORT user_count DESC
  LIMIT 10
  RETURN {city_id, user_count}
```

---

## Performance-Best-Practices (MVP)

### 1. JOIN-Optimierung

** Schlecht:** Kartesisches Produkt ohne Filter
** Gut:** Spezifische FILTER-Bedingungen, LIMIT verwenden

### 2. LET f�r Wiederverwendung

Berechnungen einmal durchf�hren, mehrfach nutzen:

```aql
FOR sale IN sales
  LET net = sale.amount
  LET tax = net * 0.19
  RETURN {net, tax, gross: net + tax}
```

### 3. FILTER vor COLLECT

Datenvolumen reduzieren bevor gruppiert wird.

---

## Implementation-Status (03.11.2025)

| Feature | Status | Notes |
|---------|--------|-------|
| **FOR** (Single) | ? Production | Vollst�ndig optimiert |
| **FOR** (Multi/Join) | ? MVP | Nested-Loop, Hash-Join geplant |
| **FILTER** | ? Production | Equality + Range + AND + OR + FULLTEXT |
| **OR-Operator** | ? Production | DNF-Konvertierung, Index-Merge |
| **FULLTEXT()** | ? Production | BM25-Ranking, Stemming, Phrasen |
| **FULLTEXT + AND** | ? Production | Hybrid Queries (BM25 n structural filters) |
| **SORT** | ? Production | Index-optimiert |
| **LIMIT** | ? Production | Offset + Count |
| **RETURN** | ? Production | Field/Object/Array |
| **LET** | ? MVP | Basis-Expressions, Arithmetik |
| **COLLECT** | ? MVP | Hash-Grouping, COUNT/SUM/AVG/MIN/MAX |
| **FULLTEXT + OR** | ?? Planned | Per-Disjunct FULLTEXT execution |
| **FULLTEXT_SCORE()** | ?? Planned | Score in RETURN-Expression |
| **Subqueries** | ?? Planned | Phase 1.4 |

---

**Dokumentations-Version:** 1.3 (03. November 2025)  
**Letzte Aktualisierung:** FULLTEXT + AND Hybrid Queries implementiert (13 Tests PASSED)

