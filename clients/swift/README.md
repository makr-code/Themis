# ThemisDB Swift Client

A modern Swift client library for ThemisDB, supporting iOS, macOS, tvOS, and watchOS.

## Features

- 🔐 **Full ACID Transaction Support** with automatic cleanup
- 🚀 **Modern Swift Concurrency** (async/await, actors)
- 📱 **Multi-Platform** - iOS 15+, macOS 12+, tvOS 15+, watchOS 8+
- 🛡️ **Type-Safe** - Generic methods with compile-time checking
- ⚡ **Isolation Levels** - READ_COMMITTED and SNAPSHOT
- 🧵 **Concurrent-Safe** - Actor-based design for thread safety
- 📦 **Swift Package Manager** support

## Requirements

- iOS 15.0+ / macOS 12.0+ / tvOS 15.0+ / watchOS 8.0+
- Xcode 14.0+
- Swift 5.9+

## Installation

### Swift Package Manager

Add the following to your `Package.swift` file:

```swift
dependencies: [
    .package(url: "https://github.com/makr-code/ThemisDB.git", from: "0.1.0")
]
```

Or in Xcode:
1. File → Add Packages
2. Enter: `https://github.com/makr-code/ThemisDB.git`
3. Select version 0.1.0-beta.1 or later

## Quick Start

```swift
import ThemisDB

// Create a client
let client = ThemisClient(endpoints: ["http://localhost:8080"])

// Basic CRUD operations
struct Account: Codable {
    let balance: Double
}

// Put a document
try await client.put(
    model: "relational",
    collection: "accounts",
    uuid: "acc1",
    data: Account(balance: 1000.0)
)

// Get a document
let account: Account = try await client.get(
    model: "relational",
    collection: "accounts",
    uuid: "acc1"
)

print("Balance: \(account.balance)")

// Delete a document
try await client.delete(
    model: "relational",
    collection: "accounts",
    uuid: "acc1"
)

// Query with AQL
let results: [Account] = try await client.query(
    aql: "FOR doc IN accounts FILTER doc.balance > 500 RETURN doc"
)
```

## Transactions

### Basic Transaction

```swift
let client = ThemisClient(endpoints: ["http://localhost:8080"])

// Begin a transaction with default options (READ_COMMITTED)
let tx = try await client.beginTransaction()

do {
    // Perform operations within transaction
    try await tx.put(
        model: "relational",
        collection: "accounts",
        uuid: "acc1",
        data: Account(balance: 1000.0)
    )
    
    let account: Account = try await tx.get(
        model: "relational",
        collection: "accounts",
        uuid: "acc1"
    )
    
    print("Balance: \(account.balance)")
    
    // Commit the transaction
    try await tx.commit()
} catch {
    // Rollback on error
    try await tx.rollback()
    throw error
}
```

### Transaction with Custom Options

```swift
// SNAPSHOT isolation level with 60 second timeout
let options = TransactionOptions(
    isolationLevel: .snapshot,
    timeout: 60.0
)

let tx = try await client.beginTransaction(options: options)
```

### Money Transfer Example (ACID Guarantees)

```swift
func transferMoney(
    from: String,
    to: String,
    amount: Double
) async throws {
    let client = ThemisClient(endpoints: ["http://localhost:8080"])
    
    // Use SNAPSHOT isolation for consistency
    let options = TransactionOptions(isolationLevel: .snapshot)
    let tx = try await client.beginTransaction(options: options)
    
    do {
        // Get source account
        let sourceAccount: Account = try await tx.get(
            model: "relational",
            collection: "accounts",
            uuid: from
        )
        
        // Get destination account
        let destAccount: Account = try await tx.get(
            model: "relational",
            collection: "accounts",
            uuid: to
        )
        
        // Check sufficient funds
        guard sourceAccount.balance >= amount else {
            try await tx.rollback()
            throw TransferError.insufficientFunds
        }
        
        // Update balances
        try await tx.put(
            model: "relational",
            collection: "accounts",
            uuid: from,
            data: Account(balance: sourceAccount.balance - amount)
        )
        
        try await tx.put(
            model: "relational",
            collection: "accounts",
            uuid: to,
            data: Account(balance: destAccount.balance + amount)
        )
        
        // Commit transaction (all or nothing)
        try await tx.commit()
    } catch {
        // Rollback on any error
        try await tx.rollback()
        throw error
    }
}

enum TransferError: Error {
    case insufficientFunds
}
```

## Query Examples

### Simple Query

```swift
struct User: Codable {
    let name: String
    let age: Int
}

let users: [User] = try await client.query(
    aql: "FOR user IN users FILTER user.age >= 18 RETURN user"
)
```

### Query within Transaction

```swift
let tx = try await client.beginTransaction()

do {
    let activeUsers: [User] = try await tx.query(
        aql: """
        FOR user IN users
        FILTER user.active == true
        SORT user.name ASC
        RETURN user
        """
    )
    
    // Process results
    for user in activeUsers {
        print("\(user.name): \(user.age)")
    }
    
    try await tx.commit()
} catch {
    try await tx.rollback()
    throw error
}
```

## Isolation Levels

ThemisDB supports two isolation levels:

### READ_COMMITTED (Default)
- Prevents dirty reads
- Allows non-repeatable reads and phantom reads
- Best for high concurrency scenarios

```swift
let options = TransactionOptions(isolationLevel: .readCommitted)
let tx = try await client.beginTransaction(options: options)
```

### SNAPSHOT
- Prevents dirty reads, non-repeatable reads, and phantom reads
- Provides a consistent snapshot of the database
- Best for reporting and consistency-critical operations

```swift
let options = TransactionOptions(isolationLevel: .snapshot)
let tx = try await client.beginTransaction(options: options)
```

## Error Handling

```swift
do {
    let account: Account = try await client.get(
        model: "relational",
        collection: "accounts",
        uuid: "acc1"
    )
} catch ThemisError.httpError(let statusCode, let message) {
    print("HTTP Error \(statusCode): \(message)")
} catch ThemisError.networkError(let error) {
    print("Network error: \(error.localizedDescription)")
} catch ThemisError.decodingError(let error) {
    print("Failed to decode: \(error.localizedDescription)")
} catch {
    print("Unexpected error: \(error)")
}
```

## Configuration

### Multiple Endpoints (Failover)

```swift
let client = ThemisClient(endpoints: [
    "http://server1:8080",
    "http://server2:8080",
    "http://server3:8080"
])
```

### Custom Timeout

```swift
// 60 second timeout
let client = ThemisClient(
    endpoints: ["http://localhost:8080"],
    timeout: 60.0
)
```

## Actor-Based Concurrency

Both `ThemisClient` and `Transaction` are Swift actors, providing automatic thread-safety:

```swift
// Safe to call from multiple tasks
Task {
    let doc1: Account = try await client.get(model: "relational", collection: "accounts", uuid: "acc1")
}

Task {
    let doc2: Account = try await client.get(model: "relational", collection: "accounts", uuid: "acc2")
}
```

## API Reference

### ThemisClient

#### Initialization
```swift
init(endpoints: [String], timeout: TimeInterval = 30.0)
```

#### Methods
```swift
func get<T: Decodable>(model: String, collection: String, uuid: String) async throws -> T
func put<T: Encodable>(model: String, collection: String, uuid: String, data: T) async throws
func delete(model: String, collection: String, uuid: String) async throws
func query<T: Decodable>(aql: String) async throws -> [T]
func beginTransaction(options: TransactionOptions = TransactionOptions()) async throws -> Transaction
```

### Transaction

#### Methods
```swift
func getTransactionId() -> String
func isActive() -> Bool
func get<T: Decodable>(model: String, collection: String, uuid: String) async throws -> T
func put<T: Encodable>(model: String, collection: String, uuid: String, data: T) async throws
func delete(model: String, collection: String, uuid: String) async throws
func query<T: Decodable>(aql: String) async throws -> [T]
func commit() async throws
func rollback() async throws
```

### TransactionOptions

```swift
struct TransactionOptions {
    let isolationLevel: IsolationLevel  // .readCommitted or .snapshot
    let timeout: TimeInterval?          // Optional timeout in seconds
}
```

## Testing

Run tests with:

```bash
swift test
```

Integration tests require a running ThemisDB server at `http://localhost:8080`.

## License

Apache License 2.0

## Contributing

Contributions are welcome! Please see the main ThemisDB repository for contribution guidelines.

## Links

- [ThemisDB Documentation](https://github.com/makr-code/ThemisDB)
- [API Reference](https://github.com/makr-code/ThemisDB/blob/main/docs/)
- [Issue Tracker](https://github.com/makr-code/ThemisDB/issues)
