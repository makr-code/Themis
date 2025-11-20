# ThemisDB JavaScript/TypeScript SDK

Official JavaScript/TypeScript client for ThemisDB - A high-performance multi-model database.

## Features

- ✅ **TypeScript Support** - Full type definitions included
- ✅ **Transaction Support** - BEGIN/COMMIT/ROLLBACK with isolation levels (NEW!)
- ✅ **Multi-Model** - Relational, Graph, Vector operations
- ✅ **Query Support** - AQL (Advanced Query Language)
- ✅ **Topology-Aware** - Automatic shard routing
- ✅ **Batch Operations** - Efficient bulk operations
- ✅ **Vector Search** - Similarity search
- ✅ **Retry Logic** - Automatic retries

## Installation

```bash
npm install @themisdb/client
```

## Quick Start

```typescript
import { ThemisClient } from "@themisdb/client";

const client = new ThemisClient({
  endpoints: ["http://localhost:8080"],
});

// Basic CRUD
await client.put("relational", "users", "user1", { name: "Alice" });
const user = await client.get("relational", "users", "user1");

// Transactions (NEW!)
const tx = await client.beginTransaction();
try {
  await tx.put("relational", "accounts", "acc1", { balance: 1000 });
  await tx.put("relational", "accounts", "acc2", { balance: 500 });
  await tx.commit();
} catch (error) {
  await tx.rollback();
}
```

## API Reference

### Transaction Support

```typescript
// Begin transaction with isolation level
const tx = await client.beginTransaction({ 
  isolationLevel: "SNAPSHOT" // or "READ_COMMITTED"
});

// Operations within transaction
await tx.get("relational", "users", "user1");
await tx.put("relational", "users", "user1", data);
await tx.delete("relational", "users", "user1");
await tx.query("FOR doc IN users RETURN doc");

// Commit or rollback
await tx.commit();
await tx.rollback();

// Check transaction state
console.log(tx.isActive); // true/false
console.log(tx.transactionId); // "txn_123..."
```

## Development

| Script | Purpose |
| --- | --- |
| `npm run build` | Compile TypeScript to `dist/` |
| `npm run lint` | Run ESLint |
| `npm run test` | Run Vitest tests |

## License

Apache-2.0

