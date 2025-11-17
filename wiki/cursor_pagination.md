# Cursor-Based Pagination in Themis

## Overview

Themis unterstützt Cursor-basierte Pagination für AQL-Queries. Dabei wird auf Index-Ebene ein effizienter Startpunkt gesetzt (Start-after/Start-before), statt große Offsets zu überspringen.

## How it works (Engine)

- ORDER BY über eine Range-indexierte Spalte aktiviert einen indexbasierten Scan in Sortierreihenfolge.
- Der Cursor enthält den Primary Key der zuletzt gelieferten Entity. Der Server lädt die Entity und extrahiert den Wert der Sortierspalte, um einen Anchor (value, pk) zu bilden.
- Asc (ASC): Start strictly after (value, pk) – Einträge mit demselben Sortwert und höherem PK kommen danach.
- Desc (DESC): Start strictly before (value, pk) – Einträge mit demselben Sortwert und niedrigerem PK kommen davor.
- Für die Erkennung von `has_more` wird `LIMIT` intern als `count + 1` an die Engine übergeben und im HTTP-Pfad wieder auf `count` beschnitten.

Diese Logik vermeidet das O(N)-Skipping großer Offsets und skaliert stabil über große Datenmengen.

## HTTP API

### Request Parameters

To enable cursor-based pagination, include the following parameters in your AQL query request:

```json
{
  "query": "FOR user IN users SORT user.name ASC LIMIT 10 RETURN user",
  "use_cursor": true,
  "cursor": "optional_cursor_token_from_previous_response"
}
```

**Parameters:**
- `use_cursor` (boolean): Set to `true` to enable cursor pagination
- `cursor` (string, optional): Token from previous response's `next_cursor` field to continue pagination

### Response Format

When `use_cursor` is enabled, the response format changes from the standard format to:

```json
{
  "items": [ /* array of result entities */ ],
  "has_more": true,
  "next_cursor": "base64_encoded_cursor_token",
  "batch_size": 10
}
```

**Response Fields:**
- `items`: Array of result entities (same format as standard `entities` field)
- `has_more`: Boolean indicating if more results are available
- `next_cursor`: Cursor token to use for fetching the next page (only present if `has_more` is true)
- `batch_size`: Number of items in the current batch

### Standard Response Format (without cursor)

Without `use_cursor`, the response uses the traditional format:

```json
{
  "table": "users",
  "count": 10,
  "entities": [ /* array of result entities */ ]
}
```

## Example Usage

### First Page

```bash
curl -X POST http://localhost:8080/query/aql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "FOR user IN users SORT user.name ASC LIMIT 10 RETURN user",
    "use_cursor": true
  }'
```

**Response:**
```json
{
  "items": [
    {"_key": "alice", "name": "Alice", "age": "25"},
    {"_key": "bob", "name": "Bob", "age": "30"},
    ...
  ],
  "has_more": true,
  "next_cursor": "eyJwayI6ImJvYiIsImNvbGxlY3Rpb24iOiJ1c2VycyIsInZlcnNpb24iOjF9",
  "batch_size": 10
}
```

### Subsequent Pages

Use the `next_cursor` from the previous response:

```bash
curl -X POST http://localhost:8080/query/aql \
  -H "Content-Type: application/json" \
  -d '{
    "query": "FOR user IN users SORT user.name ASC LIMIT 10 RETURN user",
    "use_cursor": true,
    "cursor": "eyJwayI6ImJvYiIsImNvbGxlY3Rpb24iOiJ1c2VycyIsInZlcnNpb24iOjF9"
  }'
```

### Last Page

When there are no more results, `has_more` will be `false` and `next_cursor` will not be present:

```json
{
  "items": [
    {"_key": "zack", "name": "Zack", "age": "28"}
  ],
  "has_more": false,
  "batch_size": 1
}
```

## Cursor Format

Cursors are Base64-encoded JSON objects containing:
- `pk`: Primary key of the last item in the current page
- `collection`: Name of the collection being queried
- `version`: Cursor format version (for future compatibility)

Example decoded cursor:
```json
{
  "pk": "users:bob",
  "collection": "users",
  "version": 1
}
```

## Edge Cases & Semantics

- Ties (gleicher Sortwert): Reihenfolge ist deterministisch durch PK-Tiebreaker. Cursor-Anker verwendet (value, pk), dadurch keine Duplikate/Übersprünge zwischen Seiten.
- DESC-Reihenfolge: Start-before-Verhalten spiegelt die absteigende Sortierung korrekt wider, `has_more` wird via `count+1` erkannt.
- Kombination mit Filtern: Cursor-Position respektiert die aktive Filtermenge; Seiten sind konsistent mit den Filterbedingungen.
- Ungültiger Cursor: Der Server antwortet mit HTTP 400 (Bad Request) und einer Fehlernachricht.

## Notes & Limitations

- Für Cursor-Pagination ist eine sortierende Spalte mit Range-Index empfohlen, damit der indexbasierte Scan greift.
- Ohne ORDER BY kann `use_cursor` verwendet werden, jedoch ist die Ordnung dann implizit nach PK; für reproduzierbares Paging wird eine Sortierung empfohlen.

## Error Handling

### Invalid Cursor

If an invalid or expired cursor token is provided, the server returns a 400 Bad Request:

```json
{
  "error": "Invalid or expired cursor"
}
```

### Collection Mismatch

If the cursor was generated for a different collection, the server returns a 400 Bad Request:

```json
{
  "error": "Cursor collection mismatch (expected: users, got: products)"
}
```

## Best Practices

1. **Always check `has_more`**: Don't assume there are more results based on batch size alone
2. **Store cursors short-term**: Cursors are stateless but may become invalid if underlying data changes significantly
3. **Use consistent queries**: The same query should be used across pagination requests (same SORT, FILTER, etc.)
4. **Handle errors gracefully**: If a cursor becomes invalid, restart pagination from the beginning
5. **Combine with LIMIT**: Use reasonable LIMIT values to control page size (recommended: 10-100 items)

## Comparison with Offset Pagination

| Aspect | Cursor-Based | Offset-Based |
|--------|-------------|--------------|
| Performance | O(1) resume | O(N) skip |
| Consistency | Stable across pages | May skip/duplicate if data changes |
| Stateless | Yes (token-based) | Yes |
| Use Case | Large datasets, real-time data | Small datasets, random access |
| Current Support | ✅ Implemented | ✅ Implemented |

## See Also

- [AQL Syntax Guide](aql_syntax.md)
- [AQL EXPLAIN/PROFILE](aql_explain_profile.md)
- [HTTP API Reference](../openapi/openapi.yaml)
