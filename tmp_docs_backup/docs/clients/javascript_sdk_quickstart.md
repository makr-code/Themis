# ThemisDB JavaScript/TypeScript SDK Quickstart

_Stand: 10. November 2025_

Die JavaScript/TypeScript SDK (`@themisdb/sdk`) steht als Alpha-Build im Repository zur Verfügung. Die API ist noch nicht stabil – Breaking Changes sind ohne Vorankündigung möglich.

## Voraussetzungen

- Node.js 18+ oder jede Laufzeit mit globalem `fetch` (für Node <18 ein Polyfill, z. B. `cross-fetch`)
- npm oder pnpm (Beispiele nutzen npm)
- Zugriff auf mindestens einen ThemisDB-HTTP-Endpunkt (z. B. `http://127.0.0.1:8765`)
- Optional: Topologie-Endpunkt (`/_admin/cluster/topology` oder vollständige URL)

## Installation

| Ziel | Befehl |
| --- | --- |
| Entwicklung im Repo | `npm install && npm run build` innerhalb von `clients/javascript/`
| Consumption aus anderem Projekt | `npm install /pfad/zu/ThemisDB/clients/javascript` (lokale Pfad-Installation)

Hinweise:

- Die SDK ist noch nicht im npm-Registry veröffentlicht.
- Das Projekt setzt aktuell auf TypeScript 5.5.x. Stellen Sie sicher, dass Ihr Tooling kompatibel ist.
- In Node-Umgebungen ohne native Fetch-Implementierung können Sie `global.fetch = (await import("cross-fetch")).fetch;` vor der Client-Initialisierung setzen.

## Minimalbeispiel

```ts
import { ThemisClient } from "@themisdb/sdk";

const client = new ThemisClient({
   endpoints: ["http://127.0.0.1:8765"],
   namespace: "default",
   metadataEndpoint: "/_admin/cluster/topology", // optional; auch vollständige URL möglich
});

const health = await client.health();
console.log(health);
```

## Konfiguration & Topologie

| Parameter | Beschreibung |
| --- | --- |
| `endpoints` | Bootstrap-Liste verfügbarer HTTP-Basen. Wird genutzt, bis die Topologie geladen ist. |
| `metadataEndpoint` | Relativer Pfad (Default `/_admin/cluster/topology`) oder vollständige URL zum Topologie-Service. |
| `namespace` | Namespace für URN-Key-Building. Default: `default`. |
| `timeoutMs` | Timeout pro Request (Default 30 000 ms). |
| `maxRetries` | Retries für 5xx-Fehler oder Netzwerk-Transienten (Default 3). |

Beim ersten Request lädt der Client die aktuelle Shard-Topologie. Falls der Fetch fehlschlägt, arbeitet der Client mit der ursprünglichen Endpoint-Liste weiter und wirft eine `TopologyError`, damit Aufrufer proaktiv reagieren können.

## CRUD & Batch-Operationen

```ts
const userId = "550e8400-e29b-41d4-a716-446655440000";

await client.put("relational", "users", userId, { name: "Alice" });
const user = await client.get("relational", "users", userId);

const deleted = await client.delete("relational", "users", userId);
console.log(deleted); // true, falls vorhanden

const batch = await client.batchGet("relational", "users", ["1", "2", "999"]);
console.log(batch.found["1"], batch.missing, batch.errors);
```

`batchGet` verarbeitet aktuell sequentiell, um konsistente Fehlermeldungen zu liefern. Fehler pro UUID werden unter `errors` gesammelt.

## AQL-Queries & Cursor

```ts
const page = await client.query("FOR u IN users RETURN u", {
   useCursor: true,
   batchSize: 100,
});

if (page.hasMore && page.nextCursor) {
   const next = await client.query("FOR u IN users RETURN u", {
      useCursor: true,
      cursor: page.nextCursor,
   });
   console.log(next.items.length);
}
```

Das SDK erkennt URN-basierte Single-Shard-Queries (`urn:themis:`) und adressiert nur einen Knoten. Für alle anderen Queries erfolgt Scatter-Gather über die bekannte Topologie. Das Ergebnis `QueryResult` normalisiert sowohl Legacy-Felder (`entities`) als auch Cursor-Felder (`items`, `hasMore`, `nextCursor`).

## Vektor-Suche

```ts
const result = await client.vectorSearch([0.13, -0.4, 0.9], {
   topK: 5,
   filter: { namespace: "docs" },
});

console.log(result.results);
```

Der Client konsolidiert Treffer mehrerer Shards und sortiert sie nach Score (oder inverser Distanz). Cursor-Parameter werden transparent weitergereicht.

## Fehlerbehandlung

- `TopologyError` signalisiert Probleme beim Laden der Topologie.
- HTTP-Antworten mit Status ≥ 400 lösen Fehler mit detailreicher Nachricht aus (`status`, `statusText`, Body-Auszug).
- Netzwerkfehler (`TypeError`, `AbortError`) werden nach Retries weitergegeben.

Empfehlung: Umschließen Sie kritische Pfade mit eigenem Retry- oder Circuit-Breaker-Verhalten, insbesondere bei Batch-Operationen.

## Tooling & Skripte

```bash
# Im Repository
cd clients/javascript
npm install
npm run build   # TypeScript → dist/
npm run lint    # eslint + @typescript-eslint
# Tests (Skelett vorhanden, Suite folgt)
npm run test    # Vitest – aktuell Platzhalter
```

Die Lint-Konfiguration (`.eslintrc.json`) richtet sich nach TypeScript ESLint. Unit- und Integrationstests werden in den kommenden Iterationen ergänzt (`tests/`).

## Roadmap

- Fertigstellung der Vitest-Suites inkl. Mock-Server
- Verpackung & Veröffentlichung als npm-Paket
- Async-Iterator-Hilfen für Cursor-Flows (`for await`)
- Browser-spezifische Beispiele (Vite/React, Service Worker)
- Erweiterte Fehlerklassifikation (eigene Error-Typen)
