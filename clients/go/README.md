# ThemisDB Go Client

Official Go client library for [ThemisDB](https://github.com/makr-code/ThemisDB) - A high-performance multi-model database with ACID transactions, graph capabilities, and vector search.

[![Go Version](https://img.shields.io/badge/go-1.21+-blue.svg)](https://golang.org/dl/)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)

## Features

- ✅ **ACID Transactions** - Full transaction support with BEGIN/COMMIT/ROLLBACK
- ✅ **Multiple Isolation Levels** - READ_COMMITTED, SNAPSHOT
- ✅ **CRUD Operations** - Get, Put, Delete with type-safe interfaces
- ✅ **AQL Query Support** - Execute complex queries
- ✅ **Context Support** - Full context.Context integration for cancellation and timeouts
- ✅ **Concurrent-Safe** - Thread-safe transaction and client operations
- ✅ **Idiomatic Go** - Follows Go best practices and conventions

## Installation

```bash
go get github.com/makr-code/ThemisDB/clients/go
```

## Quick Start

### Basic Operations

```go
package main

import (
    "context"
    "fmt"
    "log"

    themisdb "github.com/makr-code/ThemisDB/clients/go"
)

func main() {
    // Create client
    client := themisdb.NewClient(themisdb.Config{
        Endpoints: []string{"http://localhost:8080"},
    })

    ctx := context.Background()

    // Put data
    user := map[string]interface{}{
        "name":  "Alice",
        "email": "alice@example.com",
        "age":   30,
    }
    err := client.Put(ctx, "relational", "users", "user-123", user)
    if err != nil {
        log.Fatal(err)
    }

    // Get data
    var result map[string]interface{}
    err = client.Get(ctx, "relational", "users", "user-123", &result)
    if err != nil {
        log.Fatal(err)
    }
    fmt.Printf("User: %+v\n", result)

    // Delete data
    err = client.Delete(ctx, "relational", "users", "user-123")
    if err != nil {
        log.Fatal(err)
    }
}
```

### ACID Transactions

```go
package main

import (
    "context"
    "fmt"
    "log"

    themisdb "github.com/makr-code/ThemisDB/clients/go"
)

func main() {
    client := themisdb.NewClient(themisdb.Config{
        Endpoints: []string{"http://localhost:8080"},
    })

    ctx := context.Background()

    // Begin transaction with SNAPSHOT isolation
    tx, err := client.BeginTransaction(ctx, &themisdb.TransactionOptions{
        IsolationLevel: themisdb.Snapshot,
    })
    if err != nil {
        log.Fatal(err)
    }

    // Perform operations within transaction
    account1 := map[string]interface{}{
        "name":    "Alice",
        "balance": 1000,
    }
    err = tx.Put(ctx, "relational", "accounts", "acc-1", account1)
    if err != nil {
        tx.Rollback(ctx)
        log.Fatal(err)
    }

    account2 := map[string]interface{}{
        "name":    "Bob",
        "balance": 500,
    }
    err = tx.Put(ctx, "relational", "accounts", "acc-2", account2)
    if err != nil {
        tx.Rollback(ctx)
        log.Fatal(err)
    }

    // Commit transaction
    err = tx.Commit(ctx)
    if err != nil {
        log.Fatal(err)
    }

    fmt.Println("Transaction committed successfully!")
}
```

### Money Transfer Example

```go
func transferMoney(client *themisdb.Client, fromID, toID string, amount float64) error {
    ctx := context.Background()

    // Start transaction with SNAPSHOT isolation for consistency
    tx, err := client.BeginTransaction(ctx, &themisdb.TransactionOptions{
        IsolationLevel: themisdb.Snapshot,
    })
    if err != nil {
        return fmt.Errorf("failed to begin transaction: %w", err)
    }

    // Ensure we rollback on error
    defer func() {
        if tx.IsActive() {
            tx.Rollback(ctx)
        }
    }()

    // Get source account
    var fromAccount map[string]interface{}
    err = tx.Get(ctx, "relational", "accounts", fromID, &fromAccount)
    if err != nil {
        return fmt.Errorf("failed to get source account: %w", err)
    }

    // Get destination account
    var toAccount map[string]interface{}
    err = tx.Get(ctx, "relational", "accounts", toID, &toAccount)
    if err != nil {
        return fmt.Errorf("failed to get destination account: %w", err)
    }

    // Check balance
    fromBalance := fromAccount["balance"].(float64)
    if fromBalance < amount {
        return fmt.Errorf("insufficient funds: have %.2f, need %.2f", fromBalance, amount)
    }

    // Update balances
    fromAccount["balance"] = fromBalance - amount
    toAccount["balance"] = toAccount["balance"].(float64) + amount

    // Write updates
    err = tx.Put(ctx, "relational", "accounts", fromID, fromAccount)
    if err != nil {
        return fmt.Errorf("failed to update source account: %w", err)
    }

    err = tx.Put(ctx, "relational", "accounts", toID, toAccount)
    if err != nil {
        return fmt.Errorf("failed to update destination account: %w", err)
    }

    // Commit transaction
    err = tx.Commit(ctx)
    if err != nil {
        return fmt.Errorf("failed to commit transaction: %w", err)
    }

    return nil
}
```

### Query Support

```go
func queryUsers(client *themisdb.Client) error {
    ctx := context.Background()

    // Execute AQL query
    aql := `
        FOR user IN users
        FILTER user.age >= 18
        SORT user.name ASC
        RETURN user
    `

    var users []map[string]interface{}
    err := client.Query(ctx, aql, &users)
    if err != nil {
        return err
    }

    for _, user := range users {
        fmt.Printf("User: %s (age: %v)\n", user["name"], user["age"])
    }

    return nil
}
```

### Context and Timeouts

```go
func operationWithTimeout(client *themisdb.Client) error {
    // Create context with timeout
    ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
    defer cancel()

    var result map[string]interface{}
    err := client.Get(ctx, "relational", "users", "user-123", &result)
    if err != nil {
        if ctx.Err() == context.DeadlineExceeded {
            return fmt.Errorf("operation timed out")
        }
        return err
    }

    return nil
}
```

## API Reference

### Client

#### `NewClient(config Config) *Client`

Creates a new ThemisDB client.

**Parameters:**
- `config.Endpoints` - List of ThemisDB server endpoints (default: `["http://localhost:8080"]`)
- `config.Timeout` - HTTP request timeout (default: 30s)
- `config.MaxRetries` - Maximum retries for failed requests (default: 3)

**Returns:** Configured ThemisDB client

#### `Get(ctx context.Context, model, collection, uuid string, result interface{}) error`

Retrieves an entity by UUID.

**Parameters:**
- `ctx` - Context for cancellation and timeouts
- `model` - Data model (e.g., "relational", "graph", "vector")
- `collection` - Collection name
- `uuid` - Entity UUID
- `result` - Pointer to result variable

**Returns:** Error if operation fails

#### `Put(ctx context.Context, model, collection, uuid string, data interface{}) error`

Creates or updates an entity.

**Parameters:**
- `ctx` - Context for cancellation and timeouts
- `model` - Data model
- `collection` - Collection name
- `uuid` - Entity UUID
- `data` - Entity data (will be JSON marshaled)

**Returns:** Error if operation fails

#### `Delete(ctx context.Context, model, collection, uuid string) error`

Deletes an entity by UUID.

**Parameters:**
- `ctx` - Context for cancellation and timeouts
- `model` - Data model
- `collection` - Collection name
- `uuid` - Entity UUID

**Returns:** Error if operation fails

#### `Query(ctx context.Context, aql string, result interface{}) error`

Executes an AQL query.

**Parameters:**
- `ctx` - Context for cancellation and timeouts
- `aql` - AQL query string
- `result` - Pointer to result variable (usually a slice)

**Returns:** Error if operation fails

#### `BeginTransaction(ctx context.Context, opts *TransactionOptions) (*Transaction, error)`

Starts a new ACID transaction.

**Parameters:**
- `ctx` - Context for cancellation and timeouts
- `opts` - Transaction options (nil for defaults)
  - `IsolationLevel` - READ_COMMITTED or SNAPSHOT
  - `Timeout` - Transaction timeout

**Returns:** Transaction object and error

### Transaction

#### `Get(ctx context.Context, model, collection, uuid string, result interface{}) error`

Retrieves an entity within the transaction.

#### `Put(ctx context.Context, model, collection, uuid string, data interface{}) error`

Creates or updates an entity within the transaction.

#### `Delete(ctx context.Context, model, collection, uuid string) error`

Deletes an entity within the transaction.

#### `Query(ctx context.Context, aql string, result interface{}) error`

Executes a query within the transaction.

#### `Commit(ctx context.Context) error`

Commits the transaction. All changes become visible to other transactions.

**Returns:** Error if commit fails

#### `Rollback(ctx context.Context) error`

Rolls back the transaction. All changes are discarded.

**Returns:** Error if rollback fails

#### `IsActive() bool`

Returns whether the transaction is still active.

**Returns:** true if active, false if committed or rolled back

#### `TransactionID() string`

Returns the transaction ID.

**Returns:** Transaction ID string

## Isolation Levels

### READ_COMMITTED

Guarantees that any data read is committed at the moment it is read. Allows non-repeatable reads and phantom reads.

```go
tx, err := client.BeginTransaction(ctx, &themisdb.TransactionOptions{
    IsolationLevel: themisdb.ReadCommitted,
})
```

### SNAPSHOT

Provides a consistent snapshot of the database. All reads within the transaction see the same data, even if other transactions commit changes.

```go
tx, err := client.BeginTransaction(ctx, &themisdb.TransactionOptions{
    IsolationLevel: themisdb.Snapshot,
})
```

## Error Handling

The client uses standard Go error handling patterns:

```go
err := client.Put(ctx, "relational", "users", "123", user)
if err != nil {
    // Handle error
    log.Printf("Failed to put user: %v", err)
    return err
}
```

For transactions, it's recommended to use defer for rollback:

```go
tx, err := client.BeginTransaction(ctx, nil)
if err != nil {
    return err
}

defer func() {
    if tx.IsActive() {
        tx.Rollback(ctx)
    }
}()

// Perform operations...

err = tx.Commit(ctx)
if err != nil {
    return err
}
```

## Testing

Run unit tests:

```bash
go test -v
```

Run integration tests (requires running ThemisDB server):

```bash
go test -v -tags=integration
```

## Best Practices

1. **Always use context** - Pass `context.Context` for cancellation and timeout control
2. **Handle transaction errors** - Use defer to ensure rollback on error
3. **Use appropriate isolation levels** - SNAPSHOT for consistency, READ_COMMITTED for performance
4. **Close transactions** - Always commit or rollback transactions
5. **Check IsActive()** - Verify transaction state before operations

## Examples

See the [examples](examples/) directory for more usage examples:

- Basic CRUD operations
- Transaction patterns
- Query examples
- Error handling
- Batch operations

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

Apache License 2.0 - see [LICENSE](../../LICENSE) for details.

## Support

- **Documentation:** [ThemisDB Docs](https://github.com/makr-code/ThemisDB)
- **Issues:** [GitHub Issues](https://github.com/makr-code/ThemisDB/issues)
- **Discussions:** [GitHub Discussions](https://github.com/makr-code/ThemisDB/discussions)

## Related Projects

- [JavaScript/TypeScript SDK](../javascript)
- [Python SDK](../python)
- [Rust SDK](../rust)
