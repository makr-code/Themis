# ThemisDB Rust SDK

Official Rust SDK for ThemisDB - A high-performance multi-model database with ACID transaction support.

[![Version](https://img.shields.io/badge/version-0.1.0--beta.1-blue)](https://crates.io/crates/themisdb-client)
[![License](https://img.shields.io/badge/license-Apache--2.0-green)](LICENSE)

## Features

- ✅ **Full ACID Transaction Support** with `BEGIN`, `COMMIT`, and `ROLLBACK`
- ✅ **Multi-Model Operations** - Relational, Document, Graph, Vector, Time-Series
- ✅ **Async/Await** - Built on Tokio for high-performance async operations
- ✅ **Isolation Levels** - `READ_COMMITTED` and `SNAPSHOT` isolation
- ✅ **Type-Safe** - Leverages Rust's type system with generics and serde
- ✅ **Automatic Topology Discovery** - Client-side sharding and routing
- ✅ **Batch Operations** - Optimized batch get/put operations
- ✅ **Vector Search** - Native vector similarity search support
- ✅ **Retry Logic** - Built-in retry with exponential backoff

## Installation

Add this to your `Cargo.toml`:

```toml
[dependencies]
themisdb-client = "0.1.0-beta.1"
tokio = { version = "1.40", features = ["macros", "rt-multi-thread"] }
serde = { version = "1.0", features = ["derive"] }
serde_json = "1.0"
```

## Quick Start

```rust
use themisdb_sdk::{ThemisClient, ThemisClientConfig, TransactionOptions, IsolationLevel};
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
struct Account {
    balance: i64,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Create client
    let config = ThemisClientConfig {
        endpoints: vec!["http://localhost:8080".to_string()],
        ..Default::default()
    };
    let client = ThemisClient::new(config)?;

    // Basic operations
    let account = Account { balance: 1000 };
    client.put("relational", "accounts", "acc1", &account).await?;
    
    let result: Option<Account> = client.get("relational", "accounts", "acc1").await?;
    println!("Account: {:?}", result);

    Ok(())
}
```

## Transaction Support

### Basic Transaction

```rust
use themisdb_sdk::{TransactionOptions, IsolationLevel};

async fn transfer_money(client: &ThemisClient) -> Result<(), Box<dyn std::error::Error>> {
    // Start transaction with SNAPSHOT isolation
    let options = TransactionOptions {
        isolation_level: IsolationLevel::Snapshot,
        timeout_ms: Some(10000),
    };
    
    let tx = client.begin_transaction(options).await?;
    
    // Perform operations within transaction
    let acc1: Option<Account> = tx.get("relational", "accounts", "acc1").await?;
    let acc2: Option<Account> = tx.get("relational", "accounts", "acc2").await?;
    
    if let (Some(mut from), Some(mut to)) = (acc1, acc2) {
        from.balance -= 100;
        to.balance += 100;
        
        tx.put("relational", "accounts", "acc1", &from).await?;
        tx.put("relational", "accounts", "acc2", &to).await?;
        
        tx.commit().await?;
        println!("Transfer successful!");
    } else {
        tx.rollback().await?;
        println!("Account not found, rolled back");
    }
    
    Ok(())
}
```

### Transaction with Error Handling

```rust
async fn safe_transaction(client: &ThemisClient) -> Result<(), Box<dyn std::error::Error>> {
    let tx = client.begin_transaction(TransactionOptions::default()).await?;
    
    match perform_operations(&tx).await {
        Ok(_) => {
            tx.commit().await?;
            println!("Transaction committed");
        }
        Err(e) => {
            tx.rollback().await?;
            eprintln!("Transaction rolled back: {}", e);
        }
    }
    
    Ok(())
}

async fn perform_operations(tx: &themisdb_sdk::Transaction) -> Result<(), Box<dyn std::error::Error>> {
    let data = serde_json::json!({ "value": 42 });
    tx.put("relational", "data", "key1", &data).await?;
    tx.put("relational", "data", "key2", &data).await?;
    Ok(())
}
```

## API Reference

### ThemisClient

#### CRUD Operations

```rust
// Create/Update
client.put("relational", "users", "uuid", &user).await?;

// Read
let user: Option<User> = client.get("relational", "users", "uuid").await?;

// Delete
let deleted: bool = client.delete("relational", "users", "uuid").await?;

// Batch Get
let uuids = vec!["id1".to_string(), "id2".to_string()];
let result = client.batch_get::<User>("relational", "users", &uuids).await?;
println!("Found: {:?}", result.found);
println!("Missing: {:?}", result.missing);
```

#### Query Operations

```rust
use themisdb_sdk::QueryOptions;

let options = QueryOptions::default();
let result = client.query::<User>(
    "SELECT * FROM users WHERE age > 18",
    options
).await?;

for user in result.items {
    println!("User: {:?}", user);
}
```

#### Vector Search

```rust
let embedding = vec![0.1, 0.2, 0.3, 0.4]; // 4D vector
let filter = Some(serde_json::json!({ "category": "products" }));
let results = client.vector_search(&embedding, filter, Some(10)).await?;
println!("Search results: {}", results);
```

### Transaction

#### Transaction Options

```rust
// READ_COMMITTED (default)
let options = TransactionOptions {
    isolation_level: IsolationLevel::ReadCommitted,
    timeout_ms: None,
};

// SNAPSHOT isolation with timeout
let options = TransactionOptions {
    isolation_level: IsolationLevel::Snapshot,
    timeout_ms: Some(5000), // 5 seconds
};
```

#### Transaction Operations

```rust
let tx = client.begin_transaction(options).await?;

// CRUD operations
tx.put("relational", "accounts", "acc1", &account).await?;
let acc: Option<Account> = tx.get("relational", "accounts", "acc1").await?;
tx.delete("relational", "accounts", "acc2").await?;

// Query within transaction
let query_opts = QueryOptions::default();
let results = tx.query::<Account>("SELECT * FROM accounts", query_opts).await?;

// Commit or rollback
tx.commit().await?;
// or
tx.rollback().await?;
```

## Configuration

```rust
use themisdb_sdk::ThemisClientConfig;

let config = ThemisClientConfig {
    endpoints: vec![
        "http://shard1:8080".to_string(),
        "http://shard2:8080".to_string(),
    ],
    namespace: "production".to_string(),
    timeout_ms: 30_000,
    metadata_endpoint: Some("/_admin/cluster/topology".to_string()),
    max_retries: 3,
};

let client = ThemisClient::new(config)?;
```

## Error Handling

```rust
use themisdb_sdk::ThemisError;

match client.get::<User>("relational", "users", "uuid").await {
    Ok(Some(user)) => println!("Found user: {:?}", user),
    Ok(None) => println!("User not found"),
    Err(ThemisError::Http { status, body }) => {
        eprintln!("HTTP error {}: {}", status, body);
    }
    Err(ThemisError::Transport(e)) => {
        eprintln!("Network error: {}", e);
    }
    Err(ThemisError::Transaction(msg)) => {
        eprintln!("Transaction error: {}", msg);
    }
    Err(e) => eprintln!("Error: {}", e),
}
```

## Testing

Run unit tests:

```bash
cargo test
```

Run integration tests (requires ThemisDB server running on localhost:8080):

```bash
cargo test -- --ignored
```

## Examples

### Money Transfer (ACID Transaction)

```rust
#[derive(Debug, Serialize, Deserialize)]
struct Account {
    id: String,
    balance: i64,
}

async fn transfer(
    client: &ThemisClient,
    from_id: &str,
    to_id: &str,
    amount: i64,
) -> Result<(), Box<dyn std::error::Error>> {
    let options = TransactionOptions {
        isolation_level: IsolationLevel::Snapshot,
        timeout_ms: Some(10000),
    };
    
    let tx = client.begin_transaction(options).await?;
    
    // Read accounts
    let from: Account = tx.get("relational", "accounts", from_id).await?
        .ok_or("From account not found")?;
    let to: Account = tx.get("relational", "accounts", to_id).await?
        .ok_or("To account not found")?;
    
    // Validate
    if from.balance < amount {
        tx.rollback().await?;
        return Err("Insufficient funds".into());
    }
    
    // Update balances
    let new_from = Account {
        id: from.id.clone(),
        balance: from.balance - amount,
    };
    let new_to = Account {
        id: to.id.clone(),
        balance: to.balance + amount,
    };
    
    tx.put("relational", "accounts", from_id, &new_from).await?;
    tx.put("relational", "accounts", to_id, &new_to).await?;
    
    // Commit transaction
    tx.commit().await?;
    
    println!("Transferred {} from {} to {}", amount, from_id, to_id);
    Ok(())
}
```

### Batch Operations

```rust
async fn batch_create_users(client: &ThemisClient, users: Vec<User>) -> Result<(), Box<dyn std::error::Error>> {
    for user in users {
        client.put("relational", "users", &user.id, &user).await?;
    }
    Ok(())
}

async fn batch_read_users(client: &ThemisClient, ids: Vec<String>) -> Result<(), Box<dyn std::error::Error>> {
    let result = client.batch_get::<User>("relational", "users", &ids).await?;
    
    println!("Found {} users", result.found.len());
    println!("Missing {} users", result.missing.len());
    
    for (id, user) in result.found {
        println!("User {}: {:?}", id, user);
    }
    
    Ok(())
}
```

## License

Apache-2.0

## Links

- [GitHub Repository](https://github.com/makr-code/ThemisDB)
- [Documentation](https://github.com/makr-code/ThemisDB/tree/main/docs)
- [Issue Tracker](https://github.com/makr-code/ThemisDB/issues)
