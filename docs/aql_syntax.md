# AQL - THEMIS Query Language

**Version:** 1.4  
**Datum:** 09. November 2025  
**Inspiriert von:** ArangoDB AQL, mit Fokus auf Multi-Modell-Queries

---

## Überblick

**AQL (Advanced Query Language)** ist eine deklarative SQL-ähnliche Sprache für THEMIS, optimiert für hybride Queries über relationale, Graph-, Vektor- und Dokument-Daten.

**Design-Prinzipien:**
- ✓ **Einfach:** SQL-ähnliche Syntax für schnelle Adoption
- ✓ **Mächtig:** Multi-Modell-Support (Relational, Graph, Vector)
- ✓ **Optimierbar:** Automatische Index-Auswahl via Optimizer
- ✓ **Erweiterbar:** Schrittweise Erweiterung (Aggregationen, Joins, Subqueries)

---

## Quick Start

Ein kleiner Einstieg in AQL mit den wichtigsten Mustern.

```aql
// Top-10 aktive Nutzer in Berlin
FOR u IN users
  FILTER u.city == "Berlin" AND u.active == true
  SORT u.created_at DESC
  LIMIT 10
  RETURN { name: u.name, email: u.email }

// Einfache Equality-Join (MVP)
FOR u IN users
  FOR o IN orders
  FILTER u._key == o.user_id
  LIMIT 5
  RETURN u  // Hinweis: Im JOIN-Pfad derzeit nur Variable als RETURN erlaubt

// DISTINCT auf Feld
FOR u IN users
  RETURN DISTINCT u.city
```

---

## Syntax-Übersicht

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
1. `FOR` - Iteration über Collection/Index
2. `FILTER` - Prädikat-Evaluation (mit Index-Nutzung)
3. `SORT` - Sortierung (mit Index-Nutzung wenn möglich)
4. `LIMIT` - Pagination/Offset
5. `RETURN` - Projektion (Felder/Objekte/Arrays)

---

## MVP-Einschränkungen und Hinweise

Damit Erwartungen klar sind, hier die wichtigsten Begrenzungen des aktuellen MVP:

- **OR/NOT:** Vollständig unterstützt seit v1.4
  - OR wird über DNF-Konvertierung (Disjunctive Normal Form) gehandhabt
  - NOT wird zur Laufzeit evaluiert (Post-Filter); kein Index-Pushdown
  - Mehrere FILTER-Klauseln mit OR werden via kartesisches Produkt zu DNF vereinigt
  - Bei NOT-only Filtern (ohne andere Prädikate) wird automatisch Full-Scan-Fallback aktiviert
- **DISTINCT:** Vollständig unterstützt seit v1.4
  - Hash-basierte De-Duplizierung nach Projektion
  - Funktioniert mit Skalaren und Objekten
  - LIMIT wird nach DISTINCT angewandt
- **Joins:** MVP unterstützt Equality-Joins über genau zwei FOR-Klauseln
  - Pattern: `FOR u IN users FOR o IN orders FILTER u._key == o.user_id`
  - Zusätzliche Filter pro Seite erlaubt (werden pro Seite vorgezogen)
  - HTTP-Constraint: RETURN muss aktuell eine der gebundenen Variablen sein (`u` oder `o`); konstruierte Objekte im JOIN-RETURN folgen
  - Grammatik: LET steht vor FILTER; `LET` nach `FILTER` führt zu einem Parserfehler
  - SORT im Join-Pfad derzeit nicht unterstützt; LIMIT wird nach dem Join angewandt (mit Early-Out ohne SORT)
- Feld-zu-Feld Vergleiche (z. B. `u.city == o.city`) sind im Translator nicht allgemein erlaubt; Join-spezifischer Pfad unterstützt Equality-Joins
- LET in FILTER: Einfache LET-Bindungen werden vor der Übersetzung extrahiert ("pre-extracted"); bei `explain: true` signalisiert der Plan dies mit `plan.let_pre_extracted = true`
- Subqueries und komplexe verschachtelte Ausdrücke sind (noch) eingeschränkt und werden iterativ erweitert

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

Themis unterstützt Nested-Loop- und (wo möglich) Hash-Joins über zwei Collections via sequenzielle `FOR`-Klauseln:

```aql
FOR u IN users
  FOR o IN orders
    FILTER o.user_id == u._key
  RETURN {user: u.name, order: o.id}

Hinweis (HTTP, MVP): Im JOIN-Pfad darf aktuell nur eine gebundene Variable zurückgegeben werden (z. B. `RETURN u`). Konstruierte Objekte wie oben sind geplant und außerhalb von JOINs bereits möglich.
```

**Join-Arten (MVP):**
- **Equality Join:** Verknüpfung über `FILTER var1.field == var2.field`
- **Cross Product + Filter:** Kartesisches Produkt mit nachträglicher Filterung

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
- Nested-Loop kann teuer sein bei großen Datasets (O(n·m))
- Empfehlung: FILTER-Bedingungen so spezifisch wie möglich setzen
- Hash-Join-Optimierung für 2-FOR Equality-Joins ist aktiv (Build/Probe, Filter-Pushdown)
- Verwende Indizes auf Join-Spalten (z. B. `city_id`, `_key`) wo möglich

**Multi-FOR Limitierungen (MVP):**
- Maximal 2-3 FOR-Klauseln empfohlen (Performance)
- Join-Bedingung muss in FILTER sein (keine impliziten Joins)
- Nur Equality-Joins (`==`) optimiert

**Best Practices für Multi-FOR Joins:**
- 📌 **Explizite Join-Prädikate:** Verwende immer klare Gleichheitsbedingungen zwischen FOR-Variablen
  ```aql
  FOR u IN users
    FOR o IN orders
      FILTER o.user_id == u._key  -- Expliziter Join
      RETURN {user: u.name, order: o.id}
  ```
- 📌 **Reihenfolge optimieren:** Platziere kleinere Collections zuerst für bessere Performance
- 📌 **Index-Nutzung:** Stelle sicher, dass Join-Felder (z.B. `user_id`, `_key`) indexiert sind
- 📌 **Filter kombinieren:** Nutze zusätzliche FILTER-Bedingungen, um Zwischenergebnisse zu reduzieren
  ```aql
  FOR u IN users
    FILTER u.active == true       -- Reduziert äußere Loop
    FOR o IN orders
      FILTER o.user_id == u._key AND o.status == "shipped"
      RETURN {user: u.name, order: o.id}
  ```

#### Join-Optimierungen (MVP)

- Hash-Join für 2-FOR Equality-Joins
  - Aktiv, wenn die Join-Bedingung als Gleichheit zwischen zwei Variablen-Feldern vorliegt:
    `FILTER left.field == right.field`
  - Build-Phase auf der ersten FOR-Collection, Probe-Phase auf der zweiten
  - Einseitige FILTER (nur eine Variable betroffen) werden pro Seite vorgezogen (Push-Down)
  - Die Gleichheitsbedingung wird nicht doppelt geprüft (im Probe-Schritt bereits sichergestellt)
  - Übrige Multi-Variablen-FILTER werden nach dem Join angewandt
- Nested-Loop als Fallback
  - Für nicht-gleichheitsbasierte Bedingungen oder >2 FOR-Klauseln
  - Ebenfalls mit FILTER-Push-Down je Seite
- LIMIT Early-Out
  - Ohne SORT bricht der Executor früher ab, sobald `offset + count` Ergebnisse erreicht sind
  - Mit SORT wird LIMIT erst nach der Sortierung angewandt (kein Early-Out)
- LET-Unterstützung
  - LET-Bindings werden je Treffer-Kontext ausgewertet und können in FILTER verwendet werden
  - HTTP-Constraint: Im JOIN-Pfad darf RETURN aktuell nur `left` oder `right` Variable sein; RETURN von LET/ausdrücken folgt

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
FILTER NOT (doc.city == "Berlin")
FILTER doc.age >= 30 AND NOT (doc.status == "inactive")
```

**OR-Operator (vollständig unterstützt seit v1.4):**
```aql
// Einfaches OR
FILTER doc.status == "active" OR doc.status == "pending"

// OR mit AND kombiniert
FILTER (doc.status == "active" AND doc.age >= 30) OR doc.city == "Berlin"

// Komplexe DNF-Expansion
FILTER (doc.city == "Berlin" OR doc.city == "Munich") AND doc.status == "active"

// Mehrere FILTER-Klauseln mit OR (DNF-Merge)
FILTER doc.city == "Berlin" OR doc.city == "Munich"
FILTER doc.age >= 30
// → Wird intern zu DNF konvertiert: (city=Berlin AND age>=30) OR (city=Munich AND age>=30)
```

**NOT-Operator (vollständig unterstützt seit v1.4):**
```aql
// Einfache Negation
FILTER NOT (doc.city == "Berlin")

// NOT mit AND kombiniert
FILTER doc.age >= 30 AND NOT (doc.city == "Berlin")

// NOT in komplexen Ausdrücken
FILTER (doc.status == "active" OR doc.status == "pending") AND NOT doc.deleted

// Planner rewrites einfache Vergleiche für Index-Pushdown
FILTER NOT (doc.age < 30)                 // → doc.age >= 30
FILTER NOT (doc.status == "inactive")    // → doc.status < "inactive" OR doc.status > "inactive"
```

**Hinweise zu OR/NOT:**
- **OR:** Vollständig unterstützt über DNF-Konvertierung (Disjunctive Normal Form)
  - Translator konvertiert OR-Ausdrücke in Disjunktionen
  - Mehrere FILTER-Klauseln mit OR werden via kartesisches Produkt zu DNF vereinigt
  - FULLTEXT kann in OR-Ausdrücken verwendet werden
- **NOT:** Vergleichsoperatoren werden via De-Morgan/Komplement-Regeln in Index-Pushdown übersetzt; komplexe Ausdrücke (Funktionen, Subqueries) fallen auf Runtime-Post-Filter zurück
  - Beispiele: `NOT (x < v)` → `x >= v`, `NOT (x <= v)` → `x > v`, `NOT (x == v)` → `(x < v) OR (x > v)`
  - Enthalten Filter ausschließlich nicht-pushdown-fähige NOT-Ausdrücke, wird weiterhin ein Full-Scan-Fallback aktiviert
  - NOT kann mit AND/OR kombiniert werden und fügt sich in die DNF-Konvertierung ein
- **Performance:** OR/NOT können in manchen Fällen Index-Nutzung einschränken; für optimale Performance strukturelle Prädikate mit OR/NOT kombinieren

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

**FULLTEXT-Funktionsdetails (aktualisiert v1.4):**
- **Argumente:** `FULLTEXT(field, query [, limit])`
  - `field` - Spaltenname mit Fulltext-Index
  - `query` - Suchquery (Tokens mit AND-Logik, oder `"phrase"` f�r exakte Phrasen)
  - `limit` - Optional: Max. Ergebnisse (default 1000)
- **Ranking:** BM25-Scoring (k1=1.2, b=0.75)
- **Features:** Stemming (EN/DE), Stopwords, Normalization (Umlaute)
- **Hybrid Queries (v1.4):** 
  - ? `FULLTEXT(...) AND <predicates>` - Intersection-based (BM25 + strukturelle Filter)
  - ? `FULLTEXT(...) OR <expr>` - Volltext-Kandidaten vereinigt mit strukturellen Treffern; falls gemischte Quellen, fuer globale Relevanzsortierung `SORT BM25(doc) DESC` explizit angeben.
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

**DISTINCT - De-Duplizierung (seit v1.4):**
```aql
// Einfaches DISTINCT auf einzelnem Feld
FOR user IN users
  RETURN DISTINCT user.city

// DISTINCT auf konstruierten Objekten
FOR user IN users
  RETURN DISTINCT {city: user.city, status: user.status}

// DISTINCT mit LIMIT (LIMIT wird nach DISTINCT angewandt)
FOR user IN users
  LIMIT 0, 100
  RETURN DISTINCT user.category
```

**Hinweise zu DISTINCT:**
- Hash-basierte De-Duplizierung nach Projektion
- Funktioniert mit Skalaren (Strings, Zahlen) und Objekten
- LIMIT muss im Query vor RETURN stehen (Grammatik), wird aber nach DISTINCT ausgeführt
- Performance: O(n) mit Hash-Set; bei sehr großen Result-Sets Memory beachten

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

FAQ zu LET (MVP):
- Wo kommt LET hin? Immer vor FILTER (Grammatik). Beispiel:
  ```aql
  FOR u IN users
    LET n = u.name
    FILTER u.age >= 18
    RETURN u
  ```
- Kann ich im JOIN `RETURN` eine LET-Variable zurückgeben? Aktuell nein; im JOIN-Pfad nur `u` oder `o`. Außerhalb von JOINs ja.
- Darf LET mehrere Ausdrücke kombinieren? Ja, solange die Ausdrücke Basis sind (Feldzugriffe, Arithmetik, Literale).

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

**Mehrere GROUP-BY Felder:**
```aql
FOR order IN orders
  COLLECT city = order.city, status = order.status
  AGGREGATE total = COUNT()
  RETURN {city, status, total}
```

**COLLECT mit FILTER:**
```aql
FOR user IN users
  FILTER user.age > 18
  COLLECT city = user.city
  AGGREGATE adult_count = COUNT()
  RETURN {city, adult_count}
```

**HAVING-Filter nach Aggregation:**
```aql
FOR user IN users
  COLLECT city = user.city
  AGGREGATE total = COUNT()
  HAVING total >= 100
  RETURN {city, total}
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
- Gruppierung erfolgt ueber exakte JSON-Werte der Group-Keys (String, Zahl, Bool)
- Mehrere GROUP BY-Felder werden gemeinsam gehasht und sind jetzt unterstuetzt
- HAVING-Clause (Post-Aggregation-Filter) akzeptiert Gruppen- und Aggregations-Variablen

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

Unterstützt werden Equality-Joins über genau zwei `FOR`-Klauseln mit einem Gleichheitsprädikat zwischen Variablen.

```aql
FOR u IN users
  FOR o IN orders
  FILTER u._key == o.user_id
  RETURN u
```

Eigenschaften und Einschr�nkungen (MVP):
- Genau zwei `FOR`-Klauseln; ein Equality-Prädikat `var1.field == var2.field` in `FILTER`.
- Zusätzliche `FILTER` pro Seite sind erlaubt und werden vor dem Join angewendet.
- `RETURN` muss aktuell eine der Variablen zurückgeben (typisch `u` oder `o`).
- `LIMIT` wird nach dem Join angewendet (Early-Out ohne SORT). `SORT` im Join-Pfad ist derzeit nicht unterstützt.
- `explain: true` liefert einen Plan, der den Join-Pfad ausweist; bei LET-Pre-Extraction wird `plan.let_pre_extracted = true` gesetzt.

Projektion mit LET im Join-Kontext (MVP-kompatibel):

```aql
FOR u IN users
  FOR o IN orders
  FILTER u._key == o.user_id
  LET info = { user: u.name, order: o._key }
  RETURN u          // Aktuell muss im JOIN RETURN eine gebundene Variable sein
```

Hinweis: Komplexe Projektionen im JOIN sind geplant (z. B. `RETURN info`). Außerhalb von JOINs sind konstruierte Objekte bereits unterstützt. Nutze `LIMIT` wo sinnvoll.

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

### Content/File-Funktionen

Ermöglichen Zugriff auf Metadaten und Chunks von ingestierten Dateien über die Content-Pipeline.

```aql
CONTENT_META(document_id)      // Gibt Metadaten-Objekt zurück (name, size, mimeType, etc.)
CONTENT_CHUNKS(document_id)    // Gibt Array von Chunks zurück (chunk_id, text, embedding, etc.)
```

**Beispiel - Dokument-Metadaten abfragen:**
```aql
FOR doc IN documents
  FILTER doc.status == "indexed"
  LET meta = CONTENT_META(doc._key)
  RETURN {
    id: doc._key,
    filename: meta.filename,
    size_bytes: meta.size,
    mime_type: meta.mimeType,
    pages: meta.pages
  }
```

**Beispiel - Chunks mit Volltext-Suche:**
```aql
FOR doc IN documents
  FILTER FULLTEXT(doc.content, "machine learning")
  LET chunks = CONTENT_CHUNKS(doc._key)
  RETURN {
    document: doc.title,
    chunk_count: LENGTH(chunks),
    first_chunk: chunks[0].text
  }
```

**Beispiel - Vektor-Suche über Chunks:**
```aql
FOR doc IN documents
  LET chunks = CONTENT_CHUNKS(doc._key)
  LET similar = VECTOR_SEARCH("chunks", @query_embedding, 5)
  FILTER doc._key IN similar
  RETURN {
    document: doc.title,
    relevant_chunks: chunks
  }
```

**MVP-Hinweise:**
- `CONTENT_META` und `CONTENT_CHUNKS` sind Funktionen, die vom Parser als `FunctionCallExpr` erkannt werden
- Engine-Integration erfordert Content-Storage-API (siehe `docs/content_architecture.md`)
- Chunks enthalten: `chunk_id`, `text`, `embedding`, `page_number`, `bbox` (optional)

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
- Nutzt Geo-Index für Bounding-Box-Scan
- Post-Filter für exakte Distanz-Berechnung

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
- Pre-Filter: Bitset für `price < 100 AND in_stock == true` → k-NN
- Post-Filter: k-NN (20) → Filter → Top-10

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

### Index-Hints (später)

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

// Beispiel-AST für: FOR u IN users FILTER u.age > 18 RETURN u.name
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

### Phase 4 (später):
- Joins (Multi-Collection)
- Subqueries
- Transactions (BEGIN, COMMIT, ROLLBACK)
- INSERT, UPDATE, DELETE via AQL

### Ausblick & Roadmap (High-Level)

Kurzfristig (1–2 Releases):
- Erweiterte JOIN-Projektion: `RETURN {user: u, order: o}` innerhalb von JOINs
- Mehr als 2 FOR-Klauseln mit adaptiven Strategien (Hash-Chain / Merge-Joins)
- Zusätzliche Aggregationen: `STDDEV`, `VARIANCE`, `PERCENTILE`, `UNIQUE`
- Subqueries: Inline (`LET x = (FOR ... RETURN ...)`) und korrelierte Subqueries
- Verbesserter Explain-Plan: Kosten-Schätzung und Index-Wahl Ranking

Mittelfristig (3–5 Releases):
- Window Functions (z. B. `ROW_NUMBER()`, `MOVING_AVG()`)
- Query Caching / Reusable Execution Plans
- Materialisierte Views für häufige Aggregationen / Joins
- Adaptive Fulltext + Vector Re-Ranking (Hybrid Retrieval Pipeline)
- Graph-Pattern-Matching (Cypher-inspirierte Kurznotation)

Langfristig (>5 Releases):
- Kostenbasierter Optimizer mit Statistiksammlung
- Federation: Remote-Table-Refs (z. B. andere Cluster / Storage Engines)
- Multi-Tenant Isolation mit Ressourcen-Limits je Query
- Automatische Index-Empfehlungen und Online Index-Builds
- Streaming Queries / Continuous Aggregations

Design-Prioritäten dabei: Vorhersehbare Performance, klare Fehlermeldungen, inkrementelle Erweiterbarkeit.

---

## Performance-Überlegungen

**Index-Nutzung:**
- FILTER mit `==` ? Equality-Index
- FILTER mit `>`, `<` ? Range-Index
- FILTER mit `IN` ? Batch-Lookup
- SORT ? Range-Index (wenn vorhanden)

**Optimizer-Strategien:**
- **Filter-Pushdown:** FILTER vor SORT (reduziert Sortier-Kosten)
- **Index-Auswahl:** Kleinster geschätzter Index zuerst
- **Short-Circuit:** LIMIT früh anwenden (z.B. Top-K)

**Vermeiden:**
- Full-Table-Scans ohne LIMIT
- Sortierung ohne Index auf großen Datasets
- Aggregationen ohne COLLECT (ineffizient)

---

## Kompatibilität & Erweiterungen

**ArangoDB AQL:**
- Ähnliche Syntax (FOR, FILTER, SORT, LIMIT, RETURN)
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

## FAQ – Häufige Fragen

1) Warum bekomme ich beim JOIN einen 400-Fehler: "JOIN currently supports RETURN of one bound variable"?
- Im aktuellen MVP darf im JOIN-Pfad nur eine der gebundenen Variablen zurückgegeben werden (z. B. `RETURN u` oder `RETURN o`).
- Workaround: Gib eine der Variablen zurück und projiziere außerhalb des JOINs; Unterstützung für konstruierte Objekte im JOIN folgt.

2) Meine Query mit `LET` schlägt fehl – woran liegt’s?
- `LET` muss vor `FILTER` stehen. Beispiel:
  ```aql
  FOR u IN users
    LET city = u.city
    FILTER u.age >= 18
    RETURN {name: u.name, city}
  ```

3) `SORT` in einem JOIN bringt keine Wirkung – ist das ein Bug?
- `SORT` im JOIN-Pfad ist im MVP noch nicht unterstützt. `LIMIT` funktioniert (mit Early-Out ohne SORT). Globale Sortierung erfordert derzeit einen anderen Query-Aufbau.

4) `NOT`-Filter verlangsamen meine Query – was tun?
- `NOT` wird als Runtime-Post-Filter ausgewertet (kein Index-Pushdown). Kombiniere `NOT` mit selektiven positiven Filtern, um die Kandidatenmenge zu reduzieren.

5) Volltext liefert leere Ergebnisse – brauche ich einen Index?
- Ja. Richte zuerst einen Fulltext-Index ein (siehe `docs/search/fulltext_api.md`). Ohne Index findet keine BM25-Suche statt.

6) Wie nutze ich Cursor-basierte Pagination per HTTP?
- Sende `use_cursor: true` im Request; die Antwort enthält `has_more` und `next_cursor`. Für die nächste Seite `cursor` wieder mitsenden.

7) Wann sollte ich `allow_full_scan` verwenden?
- Nur für kleine Datenmengen, Tests oder Debugging. In Produktion Indizes nutzen und `allow_full_scan` vermeiden.

8) Warum sehe ich duplizierte Werte und wie entferne ich sie?
- Nutze `RETURN DISTINCT <expr>`. DISTINCT wird nach der Projektion angewandt und dedupliziert per Hashing (Achtung auf Speicherbedarf bei großen Ergebnismengen).

9) Wieso ist meine JOIN-Query langsam?
- Prüfe, ob ein Equality-Prädikat zwischen zwei Variablen existiert (z. B. `a.ref == b._key`).
- Sorge für Indizes auf Join-Feldern, setze zusätzliche FILTER und nutze LIMIT (ohne SORT greift Early-Out).

10) Wie erhalte ich einen Explain-Plan?
- Sende den Request mit `{"explain": true}` an `POST /query/aql`. Der Plan enthält u. a. Modus, geschätzte Kosten und Flags wie `let_pre_extracted`.

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

Schlecht:
```aql
FOR a IN largeA
  FOR b IN largeB
  RETURN a   // Kein Join-Prädikat: komplettes kartesisches Produkt
```

Gut:
```aql
FOR a IN largeA
  FOR b IN largeB
  FILTER a.ref_id == b._key AND b.status == "active"
  LIMIT 100
  RETURN a
```

Hinweise:
- Immer ein Equality-Prädikat zwischen Variablen setzen (`a.ref_id == b._key`).
- Zusätzliche selektive FILTER früh setzen (Pushdown reduziert Join-Input).
- LIMIT ohne SORT ermöglicht Early-Out.

### 2. LET für Wiederverwendung

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

## Implementation-Status (09.11.2025)

| Feature | Status | Notes |
|---------|--------|-------|
| FOR (Single) | Production | Vollständig optimiert |
| FOR (Join) | MVP | 2-FOR Equality, Hash-Join + Nested-Loop Fallback |
| FILTER | Production | Equality, Range, AND, OR, NOT, FULLTEXT |
| OR-Operator | Production | DNF-Konvertierung, Index-Merge |
| NOT-Operator | Production | Runtime Post-Filter, Full-Scan-Fallback möglich |
| FULLTEXT() | Production | BM25, Stemming, Phrasen |
| FULLTEXT + AND | Production | Hybrid Intersection |
| FULLTEXT + OR | Production | Disjunktive Zusammenführung |
| FULLTEXT_SCORE() | Production | Score verfügbar in RETURN |
| SORT | Production | Index-optimiert |
| LIMIT | Production | Offset + Count, Early-Out ohne SORT |
| RETURN | Production | Feld/Objekt/Array (JOIN: eingeschränkt auf Variable) |
| DISTINCT | Production | Hash De-Duplikation nach Projektion |
| LET | MVP | Basis-Ausdrücke, Reihenfolge: vor FILTER |
| COLLECT | MVP | COUNT/SUM/AVG/MIN/MAX |
| Aggregat-Erweiterungen | Planned | STDDEV, VARIANCE, PERCENTILE |
| Subqueries | Planned | Phase 1.4 |

---

**Dokumentations-Version:** 1.4 (09. November 2025)  
**Letzte Aktualisierung:** FULLTEXT OR-Unterstützung & Ranking-Merge, AQL OR vollständig produktiv, PII-Policy/Authorization Hinweise

