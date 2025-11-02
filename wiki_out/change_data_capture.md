# Change Data Capture (CDC) - Themis

## Overview

Themis' Change Data Capture (CDC) implementation provides a minimal, append-only event log that tracks all data mutations (PUT/DELETE) in the database. This enables real-time data synchronization, audit trails, and stream processing use cases.

**Key Features (MVP, Stand jetzt):**
- Sequence-basiertes Ordering (monoton steigende `sequence`)
- Automatische Erfassung für Entity-Operationen PUT/DELETE
- Inkrementeller Abruf mit Checkpointing (`from_seq`)
- Filterung per `key_prefix`; optionaler Event-Typ-Filter auf API-Ebene
- Ereignisse mit Timestamp und frei erweiterbarem `metadata`
- Long-Polling zur Latenzreduktion; experimentelles SSE-Streaming

---

## Architecture

### Data Model

**ChangeEvent Structure:**
```json
{
  "sequence": 42,
  "type": "PUT",
  "key": "user:alice",
  "value": "{\"name\":\"Alice\",\"email\":\"alice@example.com\"}",
  "timestamp_ms": 1730294567123,
  "metadata": {
    "table": "user",
    "pk": "alice"
  }
}
```

Hinweise zu Feldern:
- `sequence`: Monoton steigende ID (uint64)
- `type`: `PUT` oder `DELETE` (Typen `TRANSACTION_COMMIT`/`TRANSACTION_ROLLBACK` sind definiert, werden aktuell aber nicht emittiert)
- `key`: Vollständiger Schlüssel, z. B. `table:pk`
- `value`: JSON-String bei PUT, `null` bei DELETE
- `timestamp_ms`: Millisekunden seit Epoch
- `metadata`: Freies JSON (z. B. `table`, `pk`)

### Storage

- **Column Family**: Default CF (or dedicated CF if configured)
- **Key Format**: `changefeed:{sequence_number}` (zero-padded for lexicographic ordering)
- **Sequence Tracking**: Atomic counter stored at key `changefeed_sequence`

---

## HTTP API

### 1. Query Changefeed Events

Retrieve change events with optional filtering and pagination.

**Endpoint:** `GET /changefeed`

**Query Parameters:**
- `from_seq` (optional): Start after this sequence number (default: 0)
- `limit` (optional): Maximum events to return (default: 100)
- `long_poll_ms` (optional): Long-poll timeout in milliseconds (default: 0 = immediate)
- `key_prefix` (optional): Filter events by key prefix (e.g., `user:`)

**Request Example:**
```bash
# Get all events
curl "http://localhost:8765/changefeed?from_seq=0&limit=20"

# Incremental query from checkpoint
curl "http://localhost:8765/changefeed?from_seq=42&limit=10"

# Filter by key prefix
curl "http://localhost:8765/changefeed?from_seq=0&limit=50&key_prefix=user:"

# Long-poll for new events
curl "http://localhost:8765/changefeed?from_seq=100&limit=10&long_poll_ms=5000"
```

**Response:**
```json
{
  "events": [
    {
      "sequence": 1,
      "type": "PUT",
      "key": "user:alice",
      "value": "{\"name\":\"Alice\",\"email\":\"alice@example.com\"}",
      "timestamp_ms": 1730294567123,
      "metadata": {"table": "user", "pk": "alice"}
    },
    {
      "sequence": 2,
      "type": "DELETE",
      "key": "user:bob",
      "value": null,
      "timestamp_ms": 1730294568456,
      "metadata": {"table": "user", "pk": "bob"}
    }
  ],
  "count": 2,
  "latest_sequence": 42
}
```

Antwortfelder:
- `events`: Liste von ChangeEvent-Objekten
- `count`: Anzahl der zurückgegebenen Events
- `latest_sequence`: Aktuell letzter Sequence-Wert in der DB (für Checkpointing hilfreich)

---

## Validation Tests (30.10.2025)

### Test 1: Automatic CDC Recording
**Status:** ✅ PASSED

- Created 4 entities via PUT → 4 CDC PUT events recorded
- Deleted 1 entity → 1 CDC DELETE event recorded
- **Result:** All mutations automatically tracked without manual intervention

### Test 2: Query API
**Status:** ✅ PASSED

- **Full Query:** Retrieved all events with `from_seq=0&limit=20`
- **Incremental Query:** Retrieved only new events after checkpoint
- **Key Prefix Filter:** Successfully filtered events by `user:` prefix
- **Pagination:** Limit parameter correctly restricts result size
- **Result:** Query API fully functional with all parameters

### Test 3: Event Structure
**Status:** ✅ PASSED

- **Event Types:** PUT and DELETE events correctly distinguished
- **Metadata:** Table and PK correctly embedded in each event
- **Timestamps:** Millisecond precision timestamps present
- **Value Handling:** PUT events include value, DELETE events have `value: null`
- **Result:** Event structure matches specification

### Test 4: Checkpointing Pattern
**Status:** ✅ PASSED

**Scenario:** Consumer reads batch → updates checkpoint → resumes from checkpoint
```
1. Consume events 1-5, checkpoint = 5
2. New events 6-7 arrive
3. Resume from checkpoint 5, receive events 6-7
```
**Result:** Sequential consumption with checkpointing works correctly

---

## Use Cases

### 1. Real-Time Data Synchronization
Stream database changes to external systems (analytics, search, caching):
```javascript
let checkpoint = 0;
setInterval(async () => {
  const res = await fetch(`/changefeed?from_seq=${checkpoint}&limit=100`);
  const data = await res.json();
  
  for (const event of data.events) {
    await externalSystem.sync(event);
    checkpoint = event.sequence;
  }
}, 1000);
```

### 2. Audit Trail & Compliance
Track all data mutations for compliance (GDPR, HIPAA):
```sql
-- Query all user data modifications
SELECT * FROM changefeed 
WHERE key LIKE 'user:%' AND timestamp_ms > '2025-01-01';
```

### 3. Materialized Views
Maintain denormalized views automatically:
```javascript
// Maintain user count by role
for (const event of events) {
  if (event.key.startsWith('user:')) {
    if (event.type === 'PUT') {
      const user = JSON.parse(event.value);
      roleCountMap[user.role] = (roleCountMap[user.role] || 0) + 1;
    } else if (event.type === 'DELETE') {
      // Decrement count
    }
  }
}
```

### 4. Event Sourcing
Rebuild application state from event log:
```javascript
// Rebuild state from beginning
const events = await fetch('/changefeed?from_seq=0&limit=1000');
const state = {};
for (const event of events.events) {
  applyEvent(state, event);
}
```

---

## Performance & Skalierung

Aktueller Stand (MVP):
- Einfache, direkte Speicherung in RocksDB (append-only)
- Sequenzvergabe zentral; ausreichend für moderate Schreibraten
- Long-Poll als einfaches Warten mit kurzer Sleep-Periode (~50ms)

Mögliche Erweiterungen (zukünftig):
1. RocksDB WAL Tailing für geringere Latenz
2. Batch-Sequenzvergabe (z. B. Blöcke reservieren)
3. Dedizierte Column Family für CDC
4. Automatische Retention/TTL
5. Push-basierte Benachrichtigungen (WebSocket)
6. Integration in Kafka

---

## Retention & Cleanup

### Retention & Cleanup

Aktuell: Admin-Endpoint `POST /changefeed/retention` mit Body `{ "before_sequence": <uint64> }` löscht Events mit kleinerer Sequence. Statistiken über `GET /changefeed/stats`.

Zukünftig möglich: TTL-/Zeit-basierte Retention und automatische Bereinigung.

---

## Limitations & Trade-offs

### Current Limitations

1. **No Transaction Isolation:** Events are recorded per-mutation, not per-transaction
2. **No Backpressure:** Unlimited event accumulation (manual cleanup required)
3. **Sequential Sequence Generation:** Single point of contention at high write rates
4. **Polling-based Long-poll:** 50ms granularity, not instant

### Trade-offs

| Feature | Benefit | Cost |
|---------|---------|------|
| Append-only log | Simple, reliable, never loses events | Linear storage growth |
| Automatic tracking | Zero code changes, transparent | Cannot disable for specific entities |
| Sequence-based | Guaranteed order, easy checkpointing | Sequential bottleneck |
| Default CF storage | Simple deployment | No isolation from application data |

---

## Integration Examples

### PowerShell Consumer
```powershell
$checkpoint = 0
while ($true) {
    $r = Invoke-RestMethod -Uri "http://localhost:8765/changefeed?from_seq=$checkpoint&limit=100"
    
    foreach ($event in $r.events) {
        Write-Host "[$($event.sequence)] $($event.type) $($event.key)"
        
        # Process event
        if ($event.type -eq "PUT") {
            $data = $event.value | ConvertFrom-Json
            # Sync to external system
        }
        
        $checkpoint = $event.sequence
    }
    
    Start-Sleep -Seconds 1
}
```

### Python Consumer
```python
import requests
import time

checkpoint = 0
while True:
    r = requests.get(f'http://localhost:8765/changefeed?from_seq={checkpoint}&limit=100')
    data = r.json()
    
    for event in data['events']:
        print(f"[{event['sequence']}] {event['type']} {event['key']}")
        
        # Process event
        if event['type'] == 'PUT':
            value = json.loads(event['value'])
            # Sync to external system
        
        checkpoint = event['sequence']
    
    time.sleep(1)
```

### Node.js Consumer with Long-Poll
```javascript
let checkpoint = 0;

async function consumeChangefeed() {
  while (true) {
    const res = await fetch(
      `http://localhost:8765/changefeed?from_seq=${checkpoint}&limit=100&long_poll_ms=5000`
    );
    const data = await res.json();
    
    for (const event of data.events) {
      console.log(`[${event.sequence}] ${event.type} ${event.key}`);
      
      // Process event
      if (event.type === 'PUT') {
        const value = JSON.parse(event.value);
        await processEvent(value);
      }
      
      checkpoint = event.sequence;
    }
    
    // Long-poll handles waiting, no need to sleep
  }
}

consumeChangefeed();
```

---

## Configuration

### Enable CDC Feature

**config/config.json:**
```json
{
  "features": {
    "cdc": true
  }
}
```

### Verify CDC Status

Check logs on server startup:
```
[INFO] Changefeed initialized using default CF
```

Query an endpoint to verify feature is enabled:
```bash
curl http://localhost:8765/changefeed?from_seq=0&limit=1
# Should return events, not 404
```

---

## Next Steps

### Geplante Erweiterungen
1. **RocksDB WAL Tailing:** Real-time event streaming
2. **Transaction-level Events:** Group mutations by transaction
3. **Event Notifications:** WebSocket/SSE for push-based updates
4. **Kafka Integration:** Stream to Kafka topics
5. **Schema Evolution:** Track schema changes in metadata

---

## Summary

Zusammenfassung:

- Sequence-basiertes append-only Log
- Automatisches Tracking für PUT/DELETE
- GET /changefeed mit Filter/Pagination + Long-Poll
- Experimentelles SSE-Streaming (`/changefeed/stream`) mit Keep-Alive/Heartbeats

Einsatz: Echtzeit-Sync, Audit-Trails, Event Sourcing – produktionsnah nutzbar; für sehr hohe Last ggf. Erweiterungen einplanen.
