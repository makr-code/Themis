package com.themisdb.client;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.BeforeEach;

import java.time.Duration;
import java.util.Arrays;
import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Unit tests for ThemisClient and Transaction classes
 */
class ThemisClientTest {
    private List<String> endpoints;

    @BeforeEach
    void setUp() {
        endpoints = Arrays.asList("http://localhost:8080", "http://localhost:8081");
    }

    @Test
    void testClientCreation() {
        ThemisClient client = new ThemisClient(endpoints);
        assertNotNull(client);
        assertEquals("http://localhost:8080", client.getCurrentEndpoint());
    }

    @Test
    void testClientCreationWithTimeout() {
        Duration timeout = Duration.ofSeconds(60);
        ThemisClient client = new ThemisClient(endpoints, timeout);
        assertNotNull(client);
        assertEquals(timeout, client.getTimeout());
    }

    @Test
    void testClientCreationWithEmptyEndpoints() {
        assertThrows(IllegalArgumentException.class, () -> {
            new ThemisClient(Arrays.asList());
        });
    }

    @Test
    void testClientCreationWithNullEndpoints() {
        assertThrows(IllegalArgumentException.class, () -> {
            new ThemisClient(null);
        });
    }

    @Test
    void testIsolationLevelEnum() {
        assertEquals("READ_COMMITTED", IsolationLevel.READ_COMMITTED.toString());
        assertEquals("SNAPSHOT", IsolationLevel.SNAPSHOT.toString());
    }

    @Test
    void testTransactionOptionsDefaults() {
        TransactionOptions options = new TransactionOptions();
        assertEquals(IsolationLevel.READ_COMMITTED, options.getIsolationLevel());
        assertNull(options.getTimeout());
    }

    @Test
    void testTransactionOptionsWithIsolationLevel() {
        TransactionOptions options = new TransactionOptions(IsolationLevel.SNAPSHOT);
        assertEquals(IsolationLevel.SNAPSHOT, options.getIsolationLevel());
        assertNull(options.getTimeout());
    }

    @Test
    void testTransactionOptionsWithTimeout() {
        Duration timeout = Duration.ofSeconds(30);
        TransactionOptions options = new TransactionOptions(IsolationLevel.SNAPSHOT, timeout);
        assertEquals(IsolationLevel.SNAPSHOT, options.getIsolationLevel());
        assertEquals(timeout, options.getTimeout());
    }

    @Test
    void testTransactionOptionsChaining() {
        TransactionOptions options = new TransactionOptions()
                .setIsolationLevel(IsolationLevel.SNAPSHOT)
                .setTimeout(Duration.ofSeconds(45));
        
        assertEquals(IsolationLevel.SNAPSHOT, options.getIsolationLevel());
        assertEquals(Duration.ofSeconds(45), options.getTimeout());
    }

    // Integration tests - these require a running ThemisDB server
    // Uncomment and run when server is available

    /*
    @Test
    void testGetPutIntegration() throws Exception {
        ThemisClient client = new ThemisClient(Arrays.asList("http://localhost:8080"));
        
        // Put a record
        Map<String, Object> data = new HashMap<>();
        data.put("name", "Test User");
        data.put("age", 30);
        
        String uuid = UUID.randomUUID().toString();
        client.put("document", "users", uuid, data);
        
        // Get the record back
        Map result = client.get("document", "users", uuid, Map.class);
        assertEquals("Test User", result.get("name"));
        assertEquals(30.0, ((Number)result.get("age")).doubleValue());
        
        // Clean up
        client.delete("document", "users", uuid);
    }

    @Test
    void testTransactionCommit() throws Exception {
        ThemisClient client = new ThemisClient(Arrays.asList("http://localhost:8080"));
        
        try (Transaction tx = client.beginTransaction(new TransactionOptions(IsolationLevel.SNAPSHOT))) {
            assertTrue(tx.isActive());
            
            // Put some data in transaction
            Map<String, Object> account1 = new HashMap<>();
            account1.put("balance", 1000);
            tx.put("relational", "accounts", "acc1", account1);
            
            Map<String, Object> account2 = new HashMap<>();
            account2.put("balance", 500);
            tx.put("relational", "accounts", "acc2", account2);
            
            // Commit
            tx.commit();
            assertFalse(tx.isActive());
        }
        
        // Clean up
        client.delete("relational", "accounts", "acc1");
        client.delete("relational", "accounts", "acc2");
    }

    @Test
    void testTransactionRollback() throws Exception {
        ThemisClient client = new ThemisClient(Arrays.asList("http://localhost:8080"));
        
        try (Transaction tx = client.beginTransaction()) {
            assertTrue(tx.isActive());
            
            // Put some data
            Map<String, Object> data = new HashMap<>();
            data.put("value", "test");
            tx.put("document", "test", "txtest", data);
            
            // Rollback
            tx.rollback();
            assertFalse(tx.isActive());
        }
        
        // Verify data was rolled back (should throw exception or return null)
        assertThrows(Exception.class, () -> {
            client.get("document", "test", "txtest", Map.class);
        });
    }
    */
}
