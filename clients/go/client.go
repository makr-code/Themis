package themisdb

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"sync"
	"time"
)

// IsolationLevel represents transaction isolation levels
type IsolationLevel string

const (
	// ReadCommitted allows reading committed data from other transactions
	ReadCommitted IsolationLevel = "READ_COMMITTED"
	// Snapshot provides point-in-time consistent reads
	Snapshot IsolationLevel = "SNAPSHOT"
)

// Client is the ThemisDB client
type Client struct {
	endpoints  []string
	httpClient *http.Client
	mu         sync.RWMutex
	activeIdx  int
}

// Config holds client configuration
type Config struct {
	// Endpoints is a list of ThemisDB server endpoints
	Endpoints []string
	// Timeout for HTTP requests (default: 30s)
	Timeout time.Duration
	// MaxRetries for failed requests (default: 3)
	MaxRetries int
}

// NewClient creates a new ThemisDB client
func NewClient(config Config) *Client {
	if config.Timeout == 0 {
		config.Timeout = 30 * time.Second
	}
	if config.MaxRetries == 0 {
		config.MaxRetries = 3
	}
	if len(config.Endpoints) == 0 {
		config.Endpoints = []string{"http://localhost:8080"}
	}

	return &Client{
		endpoints: config.Endpoints,
		httpClient: &http.Client{
			Timeout: config.Timeout,
		},
		activeIdx: 0,
	}
}

// Get retrieves an entity by UUID
func (c *Client) Get(ctx context.Context, model, collection, uuid string, result interface{}) error {
	path := fmt.Sprintf("/api/%s/%s/%s", model, collection, uuid)
	return c.request(ctx, "GET", path, nil, result, nil)
}

// Put creates or updates an entity
func (c *Client) Put(ctx context.Context, model, collection, uuid string, data interface{}) error {
	path := fmt.Sprintf("/api/%s/%s/%s", model, collection, uuid)
	return c.request(ctx, "PUT", path, data, nil, nil)
}

// Delete removes an entity by UUID
func (c *Client) Delete(ctx context.Context, model, collection, uuid string) error {
	path := fmt.Sprintf("/api/%s/%s/%s", model, collection, uuid)
	return c.request(ctx, "DELETE", path, nil, nil, nil)
}

// QueryResult holds query results
type QueryResult struct {
	Data interface{} `json:"data"`
}

// Query executes an AQL query
func (c *Client) Query(ctx context.Context, aql string, result interface{}) error {
	path := "/api/query"
	body := map[string]interface{}{
		"query": aql,
	}
	var queryResult QueryResult
	if err := c.request(ctx, "POST", path, body, &queryResult, nil); err != nil {
		return err
	}
	
	// Marshal and unmarshal to convert to result type
	data, err := json.Marshal(queryResult.Data)
	if err != nil {
		return fmt.Errorf("failed to marshal query result: %w", err)
	}
	if err := json.Unmarshal(data, result); err != nil {
		return fmt.Errorf("failed to unmarshal query result: %w", err)
	}
	return nil
}

// request performs an HTTP request
func (c *Client) request(ctx context.Context, method, path string, body interface{}, result interface{}, headers map[string]string) error {
	var reqBody io.Reader
	if body != nil {
		data, err := json.Marshal(body)
		if err != nil {
			return fmt.Errorf("failed to marshal request body: %w", err)
		}
		reqBody = bytes.NewReader(data)
	}

	endpoint := c.getEndpoint()
	url := endpoint + path

	req, err := http.NewRequestWithContext(ctx, method, url, reqBody)
	if err != nil {
		return fmt.Errorf("failed to create request: %w", err)
	}

	req.Header.Set("Content-Type", "application/json")
	for key, value := range headers {
		req.Header.Set(key, value)
	}

	resp, err := c.httpClient.Do(req)
	if err != nil {
		return fmt.Errorf("request failed: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		bodyBytes, _ := io.ReadAll(resp.Body)
		return fmt.Errorf("request failed with status %d: %s", resp.StatusCode, string(bodyBytes))
	}

	if result != nil && resp.StatusCode != http.StatusNoContent {
		if err := json.NewDecoder(resp.Body).Decode(result); err != nil {
			return fmt.Errorf("failed to decode response: %w", err)
		}
	}

	return nil
}

// getEndpoint returns the current active endpoint
func (c *Client) getEndpoint() string {
	c.mu.RLock()
	defer c.mu.RUnlock()
	return strings.TrimSuffix(c.endpoints[c.activeIdx], "/")
}

// TransactionOptions holds transaction configuration
type TransactionOptions struct {
	IsolationLevel IsolationLevel
	Timeout        time.Duration
}

// Transaction represents an ACID transaction
type Transaction struct {
	client        *Client
	transactionID string
	active        bool
	mu            sync.RWMutex
}

// BeginTransaction starts a new ACID transaction
func (c *Client) BeginTransaction(ctx context.Context, opts *TransactionOptions) (*Transaction, error) {
	if opts == nil {
		opts = &TransactionOptions{
			IsolationLevel: ReadCommitted,
			Timeout:        30 * time.Second,
		}
	}

	reqBody := map[string]interface{}{
		"isolation_level": string(opts.IsolationLevel),
	}
	if opts.Timeout > 0 {
		reqBody["timeout"] = opts.Timeout.Seconds()
	}

	var response struct {
		TransactionID string `json:"transaction_id"`
	}

	if err := c.request(ctx, "POST", "/transaction/begin", reqBody, &response, nil); err != nil {
		return nil, fmt.Errorf("failed to begin transaction: %w", err)
	}

	return &Transaction{
		client:        c,
		transactionID: response.TransactionID,
		active:        true,
	}, nil
}

// IsActive returns whether the transaction is still active
func (tx *Transaction) IsActive() bool {
	tx.mu.RLock()
	defer tx.mu.RUnlock()
	return tx.active
}

// TransactionID returns the transaction ID
func (tx *Transaction) TransactionID() string {
	return tx.transactionID
}

// Get retrieves an entity within the transaction
func (tx *Transaction) Get(ctx context.Context, model, collection, uuid string, result interface{}) error {
	if !tx.IsActive() {
		return ErrTransactionNotActive
	}

	path := fmt.Sprintf("/api/%s/%s/%s", model, collection, uuid)
	headers := map[string]string{
		"X-Transaction-Id": tx.transactionID,
	}
	return tx.client.request(ctx, "GET", path, nil, result, headers)
}

// Put creates or updates an entity within the transaction
func (tx *Transaction) Put(ctx context.Context, model, collection, uuid string, data interface{}) error {
	if !tx.IsActive() {
		return ErrTransactionNotActive
	}

	path := fmt.Sprintf("/api/%s/%s/%s", model, collection, uuid)
	headers := map[string]string{
		"X-Transaction-Id": tx.transactionID,
	}
	return tx.client.request(ctx, "PUT", path, data, nil, headers)
}

// Delete removes an entity within the transaction
func (tx *Transaction) Delete(ctx context.Context, model, collection, uuid string) error {
	if !tx.IsActive() {
		return ErrTransactionNotActive
	}

	path := fmt.Sprintf("/api/%s/%s/%s", model, collection, uuid)
	headers := map[string]string{
		"X-Transaction-Id": tx.transactionID,
	}
	return tx.client.request(ctx, "DELETE", path, nil, nil, headers)
}

// Query executes an AQL query within the transaction
func (tx *Transaction) Query(ctx context.Context, aql string, result interface{}) error {
	if !tx.IsActive() {
		return ErrTransactionNotActive
	}

	path := "/api/query"
	body := map[string]interface{}{
		"query": aql,
	}
	headers := map[string]string{
		"X-Transaction-Id": tx.transactionID,
	}
	var queryResult QueryResult
	if err := tx.client.request(ctx, "POST", path, body, &queryResult, headers); err != nil {
		return err
	}

	// Marshal and unmarshal to convert to result type
	data, err := json.Marshal(queryResult.Data)
	if err != nil {
		return fmt.Errorf("failed to marshal query result: %w", err)
	}
	if err := json.Unmarshal(data, result); err != nil {
		return fmt.Errorf("failed to unmarshal query result: %w", err)
	}
	return nil
}

// Commit commits the transaction
func (tx *Transaction) Commit(ctx context.Context) error {
	tx.mu.Lock()
	defer tx.mu.Unlock()

	if !tx.active {
		return ErrTransactionNotActive
	}

	reqBody := map[string]interface{}{
		"transaction_id": tx.transactionID,
	}

	if err := tx.client.request(ctx, "POST", "/transaction/commit", reqBody, nil, nil); err != nil {
		return fmt.Errorf("failed to commit transaction: %w", err)
	}

	tx.active = false
	return nil
}

// Rollback rolls back the transaction
func (tx *Transaction) Rollback(ctx context.Context) error {
	tx.mu.Lock()
	defer tx.mu.Unlock()

	if !tx.active {
		return ErrTransactionNotActive
	}

	reqBody := map[string]interface{}{
		"transaction_id": tx.transactionID,
	}

	if err := tx.client.request(ctx, "POST", "/transaction/rollback", reqBody, nil, nil); err != nil {
		return fmt.Errorf("failed to rollback transaction: %w", err)
	}

	tx.active = false
	return nil
}

// Error variables
var (
	// ErrTransactionNotActive indicates the transaction is no longer active
	ErrTransactionNotActive = fmt.Errorf("transaction is not active")
)
