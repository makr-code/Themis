# ThemisDB C# Client

Official .NET client library for ThemisDB - A high-performance multi-model database with ACID transaction support.

## Features

- ✅ **Full ACID Transaction Support** with BEGIN/COMMIT/ROLLBACK
- ✅ **Async/Await Pattern** - Modern C# async programming
- ✅ **IAsyncDisposable** - Automatic transaction cleanup with `await using`
- ✅ **Thread-Safe** - Safe for concurrent operations
- ✅ **Generic Type-Safe** - Compile-time type checking
- ✅ **Isolation Levels** - READ_COMMITTED and SNAPSHOT
- ✅ **Multiple Endpoints** - Client-side load balancing
- ✅ **.NET 6.0+** - Modern .NET support
- ✅ **Nullable Reference Types** - Enhanced null safety

## Installation

### NuGet Package Manager
```bash
Install-Package ThemisDB.Client -Version 0.1.0-beta.1
```

### .NET CLI
```bash
dotnet add package ThemisDB.Client --version 0.1.0-beta.1
```

### Package Reference
```xml
<PackageReference Include="ThemisDB.Client" Version="0.1.0-beta.1" />
```

## Quick Start

### Basic Usage

```csharp
using ThemisDB.Client;

// Create client
using var client = new ThemisClient(new[] { "http://localhost:8080" });

// Put a document
await client.PutAsync("relational", "users", "user-1", new
{
    Name = "John Doe",
    Email = "john@example.com",
    Age = 30
});

// Get a document
var user = await client.GetAsync<User>("relational", "users", "user-1");
Console.WriteLine($"User: {user.Name}");

// Delete a document
await client.DeleteAsync("relational", "users", "user-1");

// Execute AQL query
var results = await client.QueryAsync<User>("SELECT * FROM users WHERE age > 25");
foreach (var result in results)
{
    Console.WriteLine($"Name: {result.Name}, Age: {result.Age}");
}
```

### ACID Transactions

```csharp
using ThemisDB.Client;

using var client = new ThemisClient(new[] { "http://localhost:8080" });

// Using await using for automatic cleanup
await using var tx = await client.BeginTransactionAsync(new TransactionOptions
{
    IsolationLevel = IsolationLevel.Snapshot
});

try
{
    // All operations within the transaction
    await tx.PutAsync("relational", "accounts", "acc-1", new { Balance = 1000 });
    await tx.PutAsync("relational", "accounts", "acc-2", new { Balance = 500 });
    
    var account = await tx.GetAsync<Account>("relational", "accounts", "acc-1");
    Console.WriteLine($"Balance: {account.Balance}");
    
    // Commit the transaction
    await tx.CommitAsync();
}
catch (Exception ex)
{
    // Rollback on error (also happens automatically on dispose if not committed)
    await tx.RollbackAsync();
    throw;
}
```

### Money Transfer Example (ACID Guarantees)

```csharp
public async Task TransferMoney(string fromAccount, string toAccount, decimal amount)
{
    using var client = new ThemisClient(new[] { "http://localhost:8080" });
    
    await using var tx = await client.BeginTransactionAsync(new TransactionOptions
    {
        IsolationLevel = IsolationLevel.Snapshot
    });
    
    try
    {
        // Read both accounts
        var from = await tx.GetAsync<Account>("relational", "accounts", fromAccount);
        var to = await tx.GetAsync<Account>("relational", "accounts", toAccount);
        
        if (from == null || to == null)
            throw new InvalidOperationException("Account not found");
        
        if (from.Balance < amount)
            throw new InvalidOperationException("Insufficient funds");
        
        // Update balances
        from.Balance -= amount;
        to.Balance += amount;
        
        // Write back
        await tx.PutAsync("relational", "accounts", fromAccount, from);
        await tx.PutAsync("relational", "accounts", toAccount, to);
        
        // Commit - both updates succeed or both fail
        await tx.CommitAsync();
        
        Console.WriteLine($"Transferred ${amount} from {fromAccount} to {toAccount}");
    }
    catch (Exception ex)
    {
        // Automatic rollback on error
        Console.WriteLine($"Transfer failed: {ex.Message}");
        throw;
    }
}

public class Account
{
    public string Id { get; set; } = string.Empty;
    public decimal Balance { get; set; }
    public string Owner { get; set; } = string.Empty;
}
```

### Querying with AQL

```csharp
// Simple query
var users = await client.QueryAsync<User>(
    "SELECT * FROM users WHERE age > 25 ORDER BY age DESC"
);

// Query within transaction
await using var tx = await client.BeginTransactionAsync();
var results = await tx.QueryAsync<Product>(
    "SELECT * FROM products WHERE price < 100"
);
await tx.CommitAsync();
```

### Cancellation Tokens

```csharp
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));

try
{
    var user = await client.GetAsync<User>(
        "relational", 
        "users", 
        "user-1", 
        cts.Token
    );
}
catch (OperationCanceledException)
{
    Console.WriteLine("Operation timed out");
}
```

## API Reference

### ThemisClient

#### Constructor
```csharp
public ThemisClient(IEnumerable<string> endpoints, TimeSpan? timeout = null)
```

Creates a new ThemisDB client instance.

- **endpoints**: List of ThemisDB server endpoints (e.g., `["http://localhost:8080"]`)
- **timeout**: Optional HTTP request timeout (default: 30 seconds)

#### Methods

##### GetAsync<T>
```csharp
public async Task<T?> GetAsync<T>(
    string model, 
    string collection, 
    string uuid, 
    CancellationToken cancellationToken = default)
```

Retrieves a document from the database.

##### PutAsync<T>
```csharp
public async Task PutAsync<T>(
    string model, 
    string collection, 
    string uuid, 
    T data, 
    CancellationToken cancellationToken = default)
```

Creates or updates a document in the database.

##### DeleteAsync
```csharp
public async Task DeleteAsync(
    string model, 
    string collection, 
    string uuid, 
    CancellationToken cancellationToken = default)
```

Deletes a document from the database.

##### QueryAsync<T>
```csharp
public async Task<List<T>> QueryAsync<T>(
    string aql, 
    CancellationToken cancellationToken = default)
```

Executes an AQL query and returns results.

##### BeginTransactionAsync
```csharp
public async Task<Transaction> BeginTransactionAsync(
    TransactionOptions? options = null, 
    CancellationToken cancellationToken = default)
```

Begins a new ACID transaction.

### Transaction

#### Properties

- `string TransactionId` - Gets the unique transaction ID
- `Task<bool> IsActiveAsync()` - Checks if the transaction is still active

#### Methods

##### GetAsync<T>
```csharp
public async Task<T?> GetAsync<T>(
    string model, 
    string collection, 
    string uuid, 
    CancellationToken cancellationToken = default)
```

Gets a document within the transaction context.

##### PutAsync<T>
```csharp
public async Task PutAsync<T>(
    string model, 
    string collection, 
    string uuid, 
    T data, 
    CancellationToken cancellationToken = default)
```

Puts a document within the transaction context.

##### DeleteAsync
```csharp
public async Task DeleteAsync(
    string model, 
    string collection, 
    string uuid, 
    CancellationToken cancellationToken = default)
```

Deletes a document within the transaction context.

##### QueryAsync<T>
```csharp
public async Task<List<T>> QueryAsync<T>(
    string aql, 
    CancellationToken cancellationToken = default)
```

Executes a query within the transaction context.

##### CommitAsync
```csharp
public async Task CommitAsync(CancellationToken cancellationToken = default)
```

Commits the transaction, making all changes permanent.

##### RollbackAsync
```csharp
public async Task RollbackAsync(CancellationToken cancellationToken = default)
```

Rolls back the transaction, discarding all changes.

### TransactionOptions

Configuration options for transactions.

```csharp
public class TransactionOptions
{
    public IsolationLevel IsolationLevel { get; set; } = IsolationLevel.ReadCommitted;
    public TimeSpan? Timeout { get; set; }
}
```

### IsolationLevel

Enum defining transaction isolation levels.

```csharp
public enum IsolationLevel
{
    ReadCommitted,  // Default - reads committed data
    Snapshot        // Repeatable reads with snapshot isolation
}
```

## Error Handling

```csharp
try
{
    await using var tx = await client.BeginTransactionAsync();
    
    await tx.PutAsync("relational", "users", "user-1", userData);
    await tx.CommitAsync();
}
catch (HttpRequestException ex)
{
    // Network or server error
    Console.WriteLine($"Request failed: {ex.Message}");
}
catch (InvalidOperationException ex)
{
    // Transaction state error
    Console.WriteLine($"Transaction error: {ex.Message}");
}
catch (OperationCanceledException)
{
    // Request was cancelled
    Console.WriteLine("Operation was cancelled");
}
```

## Best Practices

### 1. Use `await using` for Automatic Cleanup

```csharp
// ✅ Good - automatic rollback on exception
await using var tx = await client.BeginTransactionAsync();
await tx.PutAsync("relational", "test", "1", data);
await tx.CommitAsync();

// ❌ Bad - manual cleanup required
var tx = await client.BeginTransactionAsync();
try
{
    await tx.PutAsync("relational", "test", "1", data);
    await tx.CommitAsync();
}
finally
{
    await tx.DisposeAsync();
}
```

### 2. Use Cancellation Tokens

```csharp
// ✅ Good - supports cancellation
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(30));
await client.GetAsync<User>("relational", "users", "1", cts.Token);
```

### 3. Handle Errors Appropriately

```csharp
// ✅ Good - explicit error handling
try
{
    await using var tx = await client.BeginTransactionAsync();
    // ... operations
    await tx.CommitAsync();
}
catch (Exception ex)
{
    // Log error, notify user, etc.
    logger.LogError(ex, "Transaction failed");
    throw;
}
```

### 4. Use SNAPSHOT Isolation for Complex Transactions

```csharp
// ✅ Good - prevents dirty reads
var options = new TransactionOptions 
{ 
    IsolationLevel = IsolationLevel.Snapshot 
};
await using var tx = await client.BeginTransactionAsync(options);
```

## Testing

Run tests using:

```bash
dotnet test
```

Integration tests require a running ThemisDB server and are skipped by default.

## Requirements

- .NET 6.0 or later
- ThemisDB server 0.1.0+ with transaction support

## License

Apache License 2.0

## Support

- GitHub: https://github.com/makr-code/ThemisDB
- Issues: https://github.com/makr-code/ThemisDB/issues

## Contributing

Contributions are welcome! Please see the main repository for guidelines.
