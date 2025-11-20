# ThemisDB Java Client

Java client library for ThemisDB multi-model database with full ACID transaction support.

## Features

- ✅ **Full CRUD Operations** - Get, Put, Delete operations across all data models
- ✅ **ACID Transactions** - BEGIN/COMMIT/ROLLBACK with isolation level support
- ✅ **Multi-Model Support** - Relational, Document, Graph, and Vector data models
- ✅ **AQL Query Support** - Advanced Query Language for complex queries
- ✅ **Thread-Safe** - Concurrent operations with ReadWriteLock
- ✅ **Try-With-Resources** - AutoCloseable transactions for automatic cleanup
- ✅ **Type-Safe** - Generic methods with compile-time type checking
- ✅ **Java 11+** - Modern Java with java.net.http.HttpClient

## Installation

### Maven

```xml
<dependency>
    <groupId>com.themisdb</groupId>
    <artifactId>themisdb-client</artifactId>
    <version>0.1.0-beta.1</version>
</dependency>
```

### Gradle

```gradle
implementation 'com.themisdb:themisdb-client:0.1.0-beta.1'
```

## Quick Start

### Basic Operations

```java
import com.themisdb.client.ThemisClient;
import java.util.List;
import java.util.Map;
import java.util.HashMap;

public class Example {
    public static void main(String[] args) throws Exception {
        // Create client
        ThemisClient client = new ThemisClient(List.of("http://localhost:8080"));
        
        // Put a document
        Map<String, Object> user = new HashMap<>();
        user.put("name", "Alice");
        user.put("email", "alice@example.com");
        user.put("age", 30);
        
        client.put("document", "users", "user1", user);
        
        // Get the document back
        Map result = client.get("document", "users", "user1", Map.class);
        System.out.println("User: " + result.get("name"));
        
        // Delete the document
        client.delete("document", "users", "user1");
    }
}
```

### ACID Transactions

```java
import com.themisdb.client.*;
import java.util.Map;
import java.util.HashMap;

public class TransactionExample {
    public static void main(String[] args) throws Exception {
        ThemisClient client = new ThemisClient(List.of("http://localhost:8080"));
        
        // Try-with-resources automatically rolls back on exception
        try (Transaction tx = client.beginTransaction(
                new TransactionOptions(IsolationLevel.SNAPSHOT))) {
            
            // All operations are atomic
            Map<String, Object> account1 = new HashMap<>();
            account1.put("balance", 1000);
            tx.put("relational", "accounts", "acc1", account1);
            
            Map<String, Object> account2 = new HashMap<>();
            account2.put("balance", 500);
            tx.put("relational", "accounts", "acc2", account2);
            
            // Read within transaction
            Map acc1Data = tx.get("relational", "accounts", "acc1", Map.class);
            System.out.println("Account 1 balance: " + acc1Data.get("balance"));
            
            // Commit transaction
            tx.commit();
            System.out.println("Transaction committed successfully");
        } catch (Exception e) {
            // Transaction automatically rolled back
            System.err.println("Transaction failed: " + e.getMessage());
        }
    }
}
```

### Money Transfer Example (ACID Guarantees)

```java
import com.themisdb.client.*;
import java.util.Map;

public class MoneyTransferExample {
    public static void transferMoney(ThemisClient client, 
                                     String fromAccount, 
                                     String toAccount, 
                                     double amount) throws Exception {
        
        try (Transaction tx = client.beginTransaction(
                new TransactionOptions(IsolationLevel.SNAPSHOT))) {
            
            // Get current balances
            Map from = tx.get("relational", "accounts", fromAccount, Map.class);
            Map to = tx.get("relational", "accounts", toAccount, Map.class);
            
            double fromBalance = ((Number) from.get("balance")).doubleValue();
            double toBalance = ((Number) to.get("balance")).doubleValue();
            
            // Check sufficient funds
            if (fromBalance < amount) {
                throw new IllegalStateException("Insufficient funds");
            }
            
            // Update balances
            from.put("balance", fromBalance - amount);
            to.put("balance", toBalance + amount);
            
            tx.put("relational", "accounts", fromAccount, from);
            tx.put("relational", "accounts", toAccount, to);
            
            // Commit - both updates succeed or both fail
            tx.commit();
            System.out.println("Transfer completed: $" + amount);
        }
    }
    
    public static void main(String[] args) throws Exception {
        ThemisClient client = new ThemisClient(List.of("http://localhost:8080"));
        transferMoney(client, "alice", "bob", 100.0);
    }
}
```

### AQL Queries

```java
import com.themisdb.client.ThemisClient;
import java.util.List;
import java.util.Map;

public class QueryExample {
    public static void main(String[] args) throws Exception {
        ThemisClient client = new ThemisClient(List.of("http://localhost:8080"));
        
        // Execute AQL query
        String query = "SELECT * FROM users WHERE age > 25";
        List<Map> results = client.query(query, List.class);
        
        for (Map user : results) {
            System.out.println("User: " + user.get("name"));
        }
    }
}
```

### Custom Timeout

```java
import com.themisdb.client.ThemisClient;
import java.time.Duration;
import java.util.List;

public class TimeoutExample {
    public static void main(String[] args) {
        // Create client with 60-second timeout
        ThemisClient client = new ThemisClient(
            List.of("http://localhost:8080"),
            Duration.ofSeconds(60)
        );
        
        // Use client...
    }
}
```

## API Reference

### ThemisClient

#### Constructor
- `ThemisClient(List<String> endpoints)` - Create client with default 30s timeout
- `ThemisClient(List<String> endpoints, Duration timeout)` - Create client with custom timeout

#### Methods
- `<T> T get(String model, String collection, String uuid, Class<T> clazz)` - Get a record
- `void put(String model, String collection, String uuid, Object data)` - Put a record
- `void delete(String model, String collection, String uuid)` - Delete a record
- `<T> T query(String query, Class<T> clazz)` - Execute AQL query
- `Transaction beginTransaction()` - Begin transaction with default options
- `Transaction beginTransaction(TransactionOptions options)` - Begin transaction with options

### Transaction

#### Methods
- `boolean isActive()` - Check if transaction is active
- `String getTransactionId()` - Get transaction ID
- `<T> T get(String model, String collection, String uuid, Class<T> clazz)` - Get within transaction
- `void put(String model, String collection, String uuid, Object data)` - Put within transaction
- `void delete(String model, String collection, String uuid)` - Delete within transaction
- `<T> T query(String query, Class<T> clazz)` - Query within transaction
- `void commit()` - Commit transaction
- `void rollback()` - Rollback transaction
- `void close()` - AutoCloseable - automatically rolls back if active

### IsolationLevel (Enum)
- `READ_COMMITTED` - See only committed data
- `SNAPSHOT` - Work with consistent snapshot

### TransactionOptions

#### Constructor
- `TransactionOptions()` - Default (READ_COMMITTED)
- `TransactionOptions(IsolationLevel isolationLevel)` - With isolation level
- `TransactionOptions(IsolationLevel isolationLevel, Duration timeout)` - Full options

#### Methods
- `IsolationLevel getIsolationLevel()` - Get isolation level
- `TransactionOptions setIsolationLevel(IsolationLevel level)` - Set isolation level (chainable)
- `Duration getTimeout()` - Get timeout
- `TransactionOptions setTimeout(Duration timeout)` - Set timeout (chainable)

## Data Models

ThemisDB supports four data models:

1. **Relational** - Traditional tables with typed columns
2. **Document** - JSON document storage
3. **Graph** - Nodes and edges with properties
4. **Vector** - High-dimensional vector embeddings

All data models support transactions and can be used together in a single transaction.

## Error Handling

```java
import com.themisdb.client.*;
import java.io.IOException;

public class ErrorHandlingExample {
    public static void main(String[] args) {
        ThemisClient client = new ThemisClient(List.of("http://localhost:8080"));
        
        try (Transaction tx = client.beginTransaction()) {
            // Perform operations
            tx.put("document", "users", "user1", data);
            
            // Simulate error
            if (someCondition) {
                throw new RuntimeException("Business logic error");
            }
            
            tx.commit();
        } catch (IllegalStateException e) {
            // Transaction already committed/rolled back
            System.err.println("Transaction state error: " + e.getMessage());
        } catch (IOException | InterruptedException e) {
            // Network or I/O error
            System.err.println("Communication error: " + e.getMessage());
        } catch (Exception e) {
            // Other errors - transaction automatically rolled back
            System.err.println("Transaction failed: " + e.getMessage());
        }
    }
}
```

## Thread Safety

The ThemisClient and Transaction classes are thread-safe:

- **ThemisClient** - Can be shared across threads
- **Transaction** - Thread-safe with ReadWriteLock for concurrent operations
- **AutoCloseable** - Transactions automatically clean up in try-with-resources

## Building from Source

```bash
# Clone repository
git clone https://github.com/makr-code/ThemisDB.git
cd ThemisDB/clients/java

# Build with Maven
mvn clean install

# Run tests
mvn test

# Run integration tests (requires running ThemisDB server)
mvn verify -Pintegration
```

## Requirements

- Java 11 or higher
- ThemisDB server running on accessible endpoint

## Dependencies

- Gson 2.10.1 - JSON serialization
- JUnit 5 - Testing framework (test scope)

## License

Apache License 2.0

## Links

- [ThemisDB GitHub](https://github.com/makr-code/ThemisDB)
- [Documentation](https://github.com/makr-code/ThemisDB/tree/main/docs)
- [Issue Tracker](https://github.com/makr-code/ThemisDB/issues)

## Support

For questions and support:
- GitHub Issues: https://github.com/makr-code/ThemisDB/issues
- Documentation: https://github.com/makr-code/ThemisDB/tree/main/docs

## Version History

### 0.1.0-beta.1 (2024)
- Initial Beta release
- Full ACID transaction support
- Multi-model CRUD operations
- AQL query support
- Thread-safe operations
- Try-with-resources support
