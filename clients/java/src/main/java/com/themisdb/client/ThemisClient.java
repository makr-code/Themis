package com.themisdb.client;

import com.google.gson.Gson;
import com.google.gson.JsonObject;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.time.Duration;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * ThemisDB Java Client
 * 
 * A Java client library for interacting with ThemisDB multi-model database.
 * Supports CRUD operations, transactions, and multiple data models (relational, document, graph, vector).
 */
public class ThemisClient {
    private final List<String> endpoints;
    private final AtomicInteger currentEndpointIndex;
    private final HttpClient httpClient;
    private final Gson gson;
    private final Duration timeout;

    /**
     * Create a new ThemisDB client
     * 
     * @param endpoints List of ThemisDB server endpoints (e.g., ["http://localhost:8080"])
     */
    public ThemisClient(List<String> endpoints) {
        this(endpoints, Duration.ofSeconds(30));
    }

    /**
     * Create a new ThemisDB client with custom timeout
     * 
     * @param endpoints List of ThemisDB server endpoints
     * @param timeout Request timeout duration
     */
    public ThemisClient(List<String> endpoints, Duration timeout) {
        if (endpoints == null || endpoints.isEmpty()) {
            throw new IllegalArgumentException("At least one endpoint is required");
        }
        this.endpoints = endpoints;
        this.currentEndpointIndex = new AtomicInteger(0);
        this.httpClient = HttpClient.newBuilder()
                .connectTimeout(timeout)
                .build();
        this.gson = new Gson();
        this.timeout = timeout;
    }

    /**
     * Get the current endpoint URL
     * 
     * @return Current endpoint URL
     */
    public String getCurrentEndpoint() {
        int index = currentEndpointIndex.get() % endpoints.size();
        return endpoints.get(index);
    }

    /**
     * Rotate to the next endpoint (for failover)
     */
    private void rotateEndpoint() {
        currentEndpointIndex.incrementAndGet();
    }

    /**
     * Get a value from the database
     * 
     * @param model Data model (e.g., "relational", "document", "graph", "vector")
     * @param collection Collection name
     * @param uuid Record UUID
     * @param <T> Type to deserialize the response to
     * @return The retrieved value
     * @throws IOException If the request fails
     */
    public <T> T get(String model, String collection, String uuid, Class<T> clazz) throws IOException, InterruptedException {
        String url = String.format("%s/api/%s/%s/%s", getCurrentEndpoint(), model, collection, uuid);
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .GET()
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200) {
            throw new IOException("GET request failed with status: " + response.statusCode());
        }

        return gson.fromJson(response.body(), clazz);
    }

    /**
     * Put a value into the database
     * 
     * @param model Data model
     * @param collection Collection name
     * @param uuid Record UUID
     * @param data Data to store
     * @throws IOException If the request fails
     */
    public void put(String model, String collection, String uuid, Object data) throws IOException, InterruptedException {
        String url = String.format("%s/api/%s/%s/%s", getCurrentEndpoint(), model, collection, uuid);
        String jsonBody = gson.toJson(data);
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("Content-Type", "application/json")
                .PUT(HttpRequest.BodyPublishers.ofString(jsonBody))
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200 && response.statusCode() != 201) {
            throw new IOException("PUT request failed with status: " + response.statusCode());
        }
    }

    /**
     * Delete a value from the database
     * 
     * @param model Data model
     * @param collection Collection name
     * @param uuid Record UUID
     * @throws IOException If the request fails
     */
    public void delete(String model, String collection, String uuid) throws IOException, InterruptedException {
        String url = String.format("%s/api/%s/%s/%s", getCurrentEndpoint(), model, collection, uuid);
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .DELETE()
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200 && response.statusCode() != 204) {
            throw new IOException("DELETE request failed with status: " + response.statusCode());
        }
    }

    /**
     * Execute a query using AQL (Advanced Query Language)
     * 
     * @param query AQL query string
     * @param <T> Type to deserialize the response to
     * @return Query results
     * @throws IOException If the request fails
     */
    public <T> T query(String query, Class<T> clazz) throws IOException, InterruptedException {
        String url = String.format("%s/query", getCurrentEndpoint());
        JsonObject requestBody = new JsonObject();
        requestBody.addProperty("query", query);
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(gson.toJson(requestBody)))
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200) {
            throw new IOException("QUERY request failed with status: " + response.statusCode());
        }

        return gson.fromJson(response.body(), clazz);
    }

    /**
     * Begin a new transaction
     * 
     * @return Transaction object
     * @throws IOException If the request fails
     */
    public Transaction beginTransaction() throws IOException, InterruptedException {
        return beginTransaction(new TransactionOptions());
    }

    /**
     * Begin a new transaction with options
     * 
     * @param options Transaction options (isolation level, timeout)
     * @return Transaction object
     * @throws IOException If the request fails
     */
    public Transaction beginTransaction(TransactionOptions options) throws IOException, InterruptedException {
        String url = String.format("%s/transaction/begin", getCurrentEndpoint());
        JsonObject requestBody = new JsonObject();
        requestBody.addProperty("isolation_level", options.getIsolationLevel().toString());
        if (options.getTimeout() != null) {
            requestBody.addProperty("timeout", options.getTimeout().toMillis());
        }
        
        HttpRequest request = HttpRequest.newBuilder()
                .uri(URI.create(url))
                .timeout(timeout)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(gson.toJson(requestBody)))
                .build();

        HttpResponse<String> response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
        
        if (response.statusCode() != 200 && response.statusCode() != 201) {
            throw new IOException("BEGIN TRANSACTION failed with status: " + response.statusCode());
        }

        JsonObject responseBody = gson.fromJson(response.body(), JsonObject.class);
        String transactionId = responseBody.get("transaction_id").getAsString();
        
        return new Transaction(this, transactionId, httpClient, gson, timeout);
    }

    /**
     * Get the HTTP client
     * 
     * @return HttpClient instance
     */
    HttpClient getHttpClient() {
        return httpClient;
    }

    /**
     * Get the Gson instance
     * 
     * @return Gson instance
     */
    Gson getGson() {
        return gson;
    }

    /**
     * Get the timeout duration
     * 
     * @return Timeout duration
     */
    Duration getTimeout() {
        return timeout;
    }
}
