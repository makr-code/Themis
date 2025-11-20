package com.themisdb.client;

import com.google.gson.Gson;
import com.google.gson.JsonObject;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * Transaction represents an ACID transaction in ThemisDB
 * 
 * Provides transactional CRUD operations with isolation guarantees.
 * All operations within a transaction are atomic - they either all succeed or all fail.
 */
public class Transaction implements AutoCloseable {
    private final ThemisClient client;
    private final String transactionId;
    private final HttpClient httpClient;
    private final Gson gson;
    private final Duration timeout;
    private final ReadWriteLock lock;
    private boolean active;

    /**
     * Create a new transaction (package-private, use ThemisClient.beginTransaction)
     * 
     * @param client Parent ThemisClient
     * @param transactionId Transaction ID
     * @param httpClient HTTP client
     * @param gson Gson instance
     * @param timeout Request timeout
     */
    Transaction(ThemisClient client, String transactionId, HttpClient httpClient, Gson gson, Duration timeout) {
        this.client = client;
        this.transactionId = transactionId;
        this.httpClient = httpClient;
        this.gson = gson;
        this.timeout = timeout;
        this.lock = new ReentrantReadWriteLock();
        this.active = true;
    }

    /**
     * Check if the transaction is still active
     * 
     * @return true if active, false otherwise
     */
    public boolean isActive() {
        lock.readLock().lock();
        try {
            return active;
        } finally {
            lock.readLock().unlock();
        }
    }

    /**
     * Get the transaction ID
     * 
     * @return Transaction ID
     */
    public String getTransactionId() {
        return transactionId;
    }

    /**
     * Ensure transaction is active
     * 
     * @throws IllegalStateException if transaction is not active
     */
    private void ensureActive() {
        if (!isActive()) {
            throw new IllegalStateException("Transaction is not active");
        }
    }

    /**
     * Get a value within the transaction
     * 
     * @param model Data model
     * @param collection Collection name
     * @param uuid Record UUID
     * @param <T> Type to deserialize to
     * @return Retrieved value
     * @throws IOException If the request fails
     */
    public <T> T get(String model, String collection, String uuid, Class<T> clazz) throws IOException, InterruptedException {
        ensureActive();
        
        String url = String.format("%s/api/%s/%s/%s", client.getCurrentEndpoint(), model, collection, uuid);
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("X-Transaction-Id", transactionId)
                .GET()
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200) {
            throw new IOException("GET request failed with status: " + response.statusCode());
        }

        return gson.fromJson(response.body(), clazz);
    }

    /**
     * Put a value within the transaction
     * 
     * @param model Data model
     * @param collection Collection name
     * @param uuid Record UUID
     * @param data Data to store
     * @throws IOException If the request fails
     */
    public void put(String model, String collection, String uuid, Object data) throws IOException, InterruptedException {
        ensureActive();
        
        String url = String.format("%s/api/%s/%s/%s", client.getCurrentEndpoint(), model, collection, uuid);
        String jsonBody = gson.toJson(data);
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("Content-Type", "application/json")
                .header("X-Transaction-Id", transactionId)
                .PUT(HttpRequest.BodyPublishers.ofString(jsonBody))
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200 && response.statusCode() != 201) {
            throw new IOException("PUT request failed with status: " + response.statusCode());
        }
    }

    /**
     * Delete a value within the transaction
     * 
     * @param model Data model
     * @param collection Collection name
     * @param uuid Record UUID
     * @throws IOException If the request fails
     */
    public void delete(String model, String collection, String uuid) throws IOException, InterruptedException {
        ensureActive();
        
        String url = String.format("%s/api/%s/%s/%s", client.getCurrentEndpoint(), model, collection, uuid);
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("X-Transaction-Id", transactionId)
                .DELETE()
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200 && response.statusCode() != 204) {
            throw new IOException("DELETE request failed with status: " + response.statusCode());
        }
    }

    /**
     * Execute a query within the transaction
     * 
     * @param query AQL query string
     * @param <T> Type to deserialize to
     * @return Query results
     * @throws IOException If the request fails
     */
    public <T> T query(String query, Class<T> clazz) throws IOException, InterruptedException {
        ensureActive();
        
        String url = String.format("%s/query", client.getCurrentEndpoint());
        JsonObject requestBody = new JsonObject();
        requestBody.addProperty("query", query);
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("Content-Type", "application/json")
                .header("X-Transaction-Id", transactionId)
                .POST(HttpRequest.BodyPublishers.ofString(gson.toJson(requestBody)))
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200) {
            throw new IOException("QUERY request failed with status: " + response.statusCode());
        }

        return gson.fromJson(response.body(), clazz);
    }

    /**
     * Commit the transaction
     * 
     * Makes all changes permanent. After commit, the transaction is no longer active.
     * 
     * @throws IOException If the request fails
     */
    public void commit() throws IOException, InterruptedException {
        lock.writeLock().lock();
        try {
            ensureActive();
            
            String url = String.format("%s/transaction/commit", client.getCurrentEndpoint());
            JsonObject requestBody = new JsonObject();
            requestBody.addProperty("transaction_id", transactionId);
            
            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(url))
                    .timeout(timeout)
                    .header("Content-Type", "application/json")
                    .POST(HttpRequest.BodyPublishers.ofString(gson.toJson(requestBody)))
                    .build();

            HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
            
            if (response.statusCode() != 200) {
                throw new IOException("COMMIT failed with status: " + response.statusCode());
            }
            
            active = false;
        } finally {
            lock.writeLock().unlock();
        }
    }

    /**
     * Rollback the transaction
     * 
     * Discards all changes made within the transaction. After rollback, the transaction is no longer active.
     * 
     * @throws IOException If the request fails
     */
    public void rollback() throws IOException, InterruptedException {
        lock.writeLock().lock();
        try {
            if (!active) {
                return; // Already rolled back or committed
            }
            
            String url = String.format("%s/transaction/rollback", client.getCurrentEndpoint());
            JsonObject requestBody = new JsonObject();
            requestBody.addProperty("transaction_id", transactionId);
            
            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(url))
                    .timeout(timeout)
                    .header("Content-Type", "application/json")
                    .POST(HttpRequest.BodyPublishers.ofString(gson.toJson(requestBody)))
                    .build();

            HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
            
            if (response.statusCode() != 200) {
                throw new IOException("ROLLBACK failed with status: " + response.statusCode());
            }
            
            active = false;
        } finally {
            lock.writeLock().unlock();
        }
    }

    /**
     * Auto-close support for try-with-resources
     * 
     * Automatically rolls back the transaction if still active when exiting the block.
     */
    @Override
    public void close() {
        if (isActive()) {
            try {
                rollback();
            } catch (IOException | InterruptedException e) {
                // Log error but don't throw in close()
                System.err.println("Failed to rollback transaction during close: " + e.getMessage());
            }
        }
    }
}
