# ThemisDB Python SDK

Official Python client for ThemisDB - A high-performance multi-model database.

## Features

- ✅ **Type Hints** - Full type annotations (PEP 484)
- ✅ **Transaction Support** - BEGIN/COMMIT/ROLLBACK with isolation levels (NEW!)
- ✅ **Context Manager** - Pythonic `with` statement support
- ✅ **Multi-Model** - Relational, Graph, Vector operations
- ✅ **Query Support** - AQL (Advanced Query Language)
- ✅ **Topology-Aware** - Automatic shard routing
- ✅ **Batch Operations** - Efficient bulk operations
- ✅ **Vector Search** - Similarity search
- ✅ **Retry Logic** - Automatic retries

## Installation

```bash
pip install themisdb-client
```

Or for development:

```bash
pip install -e .[dev]
```

## Quick Start

```python
from themis import ThemisClient

client = ThemisClient(endpoints=["http://localhost:8080"])

# Basic CRUD
client.put("relational", "users", "user1", {"name": "Alice"})
user = client.get("relational", "users", "user1")
print(user)

# Transactions (NEW!)
tx = client.begin_transaction()
try:
    tx.put("relational", "accounts", "acc1", {"balance": 1000})
    tx.put("relational", "accounts", "acc2", {"balance": 500})
    tx.commit()
except Exception:
    tx.rollback()
    raise
```

## Transaction Support (NEW!)

ThemisDB now supports ACID transactions with BEGIN/COMMIT/ROLLBACK semantics.

### Basic Transaction Usage

```python
from themis import ThemisClient

client = ThemisClient(endpoints=["http://localhost:8080"])

# Begin a transaction
tx = client.begin_transaction()

try:
    # Perform operations within the transaction
    tx.put("relational", "accounts", "acc1", {"balance": 1000})
    tx.put("relational", "accounts", "acc2", {"balance": 500})
    
    # Read within transaction
    acc1 = tx.get("relational", "accounts", "acc1")
    print(acc1)  # {"balance": 1000}
    
    # Commit the transaction
    tx.commit()
except Exception as error:
    # Rollback on error
    tx.rollback()
    raise
```

### Context Manager (Pythonic Way)

The recommended way to use transactions in Python is with the `with` statement:

```python
# Automatically commits on success, rolls back on exception
with client.begin_transaction() as tx:
    tx.put("relational", "accounts", "acc1", {"balance": 1000})
    tx.put("relational", "accounts", "acc2", {"balance": 500})
    
    acc1 = tx.get("relational", "accounts", "acc1")
    print(acc1)  # {"balance": 1000}
    
# Transaction is automatically committed here
```

If an exception occurs, the transaction is automatically rolled back:

```python
try:
    with client.begin_transaction() as tx:
        tx.put("relational", "users", "user1", {"name": "Alice"})
        raise ValueError("Something went wrong")
except ValueError:
    pass
# Transaction was automatically rolled back
```

### Isolation Levels

ThemisDB supports two isolation levels:

- `READ_COMMITTED` (default) - Prevents dirty reads
- `SNAPSHOT` - Provides a consistent snapshot of the database

```python
# Use SNAPSHOT isolation for repeatable reads
tx = client.begin_transaction(isolation_level="SNAPSHOT")

try:
    user1 = tx.get("relational", "users", "user1")
    user2 = tx.get("relational", "users", "user2")
    
    # These reads are from the same snapshot
    # even if other transactions modify the data
    
    tx.commit()
except Exception:
    tx.rollback()
```

### Query Within Transactions

```python
with client.begin_transaction() as tx:
    # Execute AQL query within transaction
    result = tx.query("FOR user IN users FILTER user.active == true RETURN user")
    
    # Update based on query results
    for user in result.items:
        user["last_seen"] = "2025-11-20T12:00:00Z"
        tx.put("relational", "users", user["id"], user)
    
# Automatically committed
```

### Complex Example: Money Transfer

```python
def transfer_money(client, from_account: str, to_account: str, amount: float):
    """Transfer money between accounts using a transaction."""
    with client.begin_transaction(isolation_level="SNAPSHOT") as tx:
        # Read both accounts
        from_acc = tx.get("relational", "accounts", from_account)
        to_acc = tx.get("relational", "accounts", to_account)
        
        if not from_acc or not to_acc:
            raise ValueError("Account not found")
        
        if from_acc["balance"] < amount:
            raise ValueError("Insufficient funds")
        
        # Update balances
        from_acc["balance"] -= amount
        to_acc["balance"] += amount
        
        tx.put("relational", "accounts", from_account, from_acc)
        tx.put("relational", "accounts", to_account, to_acc)
        
    # Transaction automatically committed

# Usage
transfer_money(client, "alice", "bob", 100.0)
```

## API Reference

### ThemisClient

#### Constructor

```python
ThemisClient(
    endpoints: list[str],
    *,
    namespace: str = "default",
    timeout: float = 30.0,
    max_retries: int = 3,
    metadata_endpoint: str | None = None,
    metadata_path: str = "/_admin/cluster/topology",
    max_workers: int | None = None,
    transport: httpx.BaseTransport | None = None
)
```

#### Methods

- `get(model, collection, uuid)` - Retrieve an entity
- `put(model, collection, uuid, data)` - Create/update an entity
- `delete(model, collection, uuid)` - Delete an entity
- `batch_get(model, collection, uuids)` - Batch retrieve
- `batch_put(model, collection, items)` - Batch create/update
- `batch_delete(model, collection, uuids)` - Batch delete
- `query(aql, *, params=None)` - Execute AQL query
- `vector_search(embedding, top_k=10)` - Vector similarity search
- `graph_traverse(start_node, max_depth=3)` - Graph traversal
- `health(endpoint=None)` - Health check
- `begin_transaction(*, isolation_level="READ_COMMITTED")` - **NEW:** Start transaction

### Transaction

#### Properties

- `transaction_id: str` - Unique transaction identifier
- `is_active: bool` - Whether the transaction is active

#### Methods

- `get(model, collection, uuid)` - Retrieve within transaction
- `put(model, collection, uuid, data)` - Update within transaction
- `delete(model, collection, uuid)` - Delete within transaction
- `query(aql, *, params=None)` - Query within transaction
- `commit()` - Commit the transaction
- `rollback()` - Rollback the transaction

#### Context Manager

```python
with client.begin_transaction() as tx:
    # Operations here
    pass
# Automatically commits on success, rolls back on exception
```

## Development

```bash
# Install with dev dependencies
pip install -e .[dev]

# Run tests
pytest tests/

# Run tests with coverage
pytest --cov=themis tests/
```

## License

Apache-2.0

