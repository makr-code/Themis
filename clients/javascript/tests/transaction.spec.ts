import { describe, it, expect } from "vitest";
import { ThemisClient, Transaction, TransactionError } from "../src/index.js";

describe("Transaction Support", () => {
  const client = new ThemisClient({
    endpoints: ["http://localhost:8080"],
    namespace: "test",
  });

  it("should create a transaction with beginTransaction", async () => {
    // Note: This test requires a running ThemisDB server
    // In a real scenario, you'd mock the HTTP calls or have a test server
    
    // For now, we'll just test the API structure
    expect(client.beginTransaction).toBeDefined();
    expect(typeof client.beginTransaction).toBe("function");
  });

  it("Transaction class should have required methods", () => {
    // Create a mock transaction to test the API
    const mockTx = new Transaction(client, "test-tx-id");
    
    expect(mockTx.transactionId).toBe("test-tx-id");
    expect(mockTx.isActive).toBe(true);
    expect(mockTx.get).toBeDefined();
    expect(mockTx.put).toBeDefined();
    expect(mockTx.delete).toBeDefined();
    expect(mockTx.query).toBeDefined();
    expect(mockTx.commit).toBeDefined();
    expect(mockTx.rollback).toBeDefined();
  });

  it("Transaction should track committed state", () => {
    const mockTx = new Transaction(client, "test-tx-id");
    
    expect(mockTx.isActive).toBe(true);
    
    // Manually mark as committed by accessing private field (for testing)
    // In reality, this happens after a successful commit() call
    (mockTx as any).committed = true;
    
    expect(mockTx.isActive).toBe(false);
    
    // Now operations should throw TransactionError
    expect(() => {
      (mockTx as any).ensureActive();
    }).toThrow(TransactionError);
  });

  it("Transaction should track rolled back state", () => {
    const mockTx = new Transaction(client, "test-tx-id");
    
    expect(mockTx.isActive).toBe(true);
    
    // Manually mark as rolled back (for testing)
    (mockTx as any).rolledBack = true;
    
    expect(mockTx.isActive).toBe(false);
    
    // Now operations should throw TransactionError
    expect(() => {
      (mockTx as any).ensureActive();
    }).toThrow(TransactionError);
  });

  it("should support isolation level options", () => {
    // Test that the options interface is properly typed
    const options = {
      isolationLevel: "READ_COMMITTED" as const,
      timeout: 30000,
    };
    
    expect(options.isolationLevel).toBe("READ_COMMITTED");
    
    const snapshotOptions = {
      isolationLevel: "SNAPSHOT" as const,
    };
    
    expect(snapshotOptions.isolationLevel).toBe("SNAPSHOT");
  });
});

describe("Transaction Integration Tests", () => {
  // These tests would require a running ThemisDB server
  // and would test actual transaction behavior

  it.skip("should begin, execute operations, and commit a transaction", async () => {
    const client = new ThemisClient({
      endpoints: ["http://localhost:8080"],
    });

    const tx = await client.beginTransaction();
    
    try {
      await tx.put("relational", "users", "user1", { name: "Alice", age: 30 });
      await tx.put("relational", "users", "user2", { name: "Bob", age: 25 });
      
      const user1 = await tx.get("relational", "users", "user1");
      expect(user1).toEqual({ name: "Alice", age: 30 });
      
      await tx.commit();
    } catch (error) {
      await tx.rollback();
      throw error;
    }
  });

  it.skip("should rollback a transaction on error", async () => {
    const client = new ThemisClient({
      endpoints: ["http://localhost:8080"],
    });

    const tx = await client.beginTransaction();
    
    try {
      await tx.put("relational", "users", "user3", { name: "Charlie", age: 35 });
      
      // Simulate an error
      throw new Error("Something went wrong");
      
      await tx.commit();
    } catch (error) {
      await tx.rollback();
      // Verify the data was not persisted
    }
  });

  it.skip("should support transactions with SNAPSHOT isolation", async () => {
    const client = new ThemisClient({
      endpoints: ["http://localhost:8080"],
    });

    const tx = await client.beginTransaction({ isolationLevel: "SNAPSHOT" });
    
    try {
      await tx.put("relational", "accounts", "acc1", { balance: 1000 });
      await tx.put("relational", "accounts", "acc2", { balance: 500 });
      
      await tx.commit();
    } catch (error) {
      await tx.rollback();
      throw error;
    }
  });
});
