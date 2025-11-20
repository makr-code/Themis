package themisdb

import (
	"context"
	"testing"
	"time"

	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"
)

func TestNewClient(t *testing.T) {
	tests := []struct{
		name     string
		config   Config
		expected *Client
	}{
		{
			name:   "default configuration",
			config: Config{},
			expected: &Client{
				endpoints: []string{"http://localhost:8080"},
			},
		},
		{
			name: "custom endpoints",
			config: Config{
				Endpoints: []string{"http://server1:8080", "http://server2:8080"},
			},
			expected: &Client{
				endpoints: []string{"http://server1:8080", "http://server2:8080"},
			},
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			client := NewClient(tt.config)
			require.NotNil(t, client)
			assert.Equal(t, tt.expected.endpoints, client.endpoints)
			assert.NotNil(t, client.httpClient)
		})
	}
}

func TestClient_GetEndpoint(t *testing.T) {
	client := NewClient(Config{
		Endpoints: []string{"http://server1:8080/", "http://server2:8080"},
	})

	endpoint := client.getEndpoint()
	assert.Equal(t, "http://server1:8080", endpoint)
}

func TestTransaction_IsActive(t *testing.T) {
	tx := &Transaction{
		transactionID: "test-tx-id",
		active:        true,
	}

	assert.True(t, tx.IsActive())
	assert.Equal(t, "test-tx-id", tx.TransactionID())
}

func TestTransaction_InactiveState(t *testing.T) {
	client := NewClient(Config{})
	tx := &Transaction{
		client:        client,
		transactionID: "test-tx-id",
		active:        false,
	}

	ctx := context.Background()

	// Test that operations fail when transaction is not active
	err := tx.Get(ctx, "relational", "users", "123", nil)
	assert.ErrorIs(t, err, ErrTransactionNotActive)

	err = tx.Put(ctx, "relational", "users", "123", map[string]string{"name": "Alice"})
	assert.ErrorIs(t, err, ErrTransactionNotActive)

	err = tx.Delete(ctx, "relational", "users", "123")
	assert.ErrorIs(t, err, ErrTransactionNotActive)

	err = tx.Query(ctx, "SELECT * FROM users", nil)
	assert.ErrorIs(t, err, ErrTransactionNotActive)

	err = tx.Commit(ctx)
	assert.ErrorIs(t, err, ErrTransactionNotActive)

	err = tx.Rollback(ctx)
	assert.ErrorIs(t, err, ErrTransactionNotActive)
}

func TestIsolationLevel(t *testing.T) {
	tests := []struct {
		name  string
		level IsolationLevel
	}{
		{"ReadCommitted", ReadCommitted},
		{"Snapshot", Snapshot},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			assert.NotEmpty(t, string(tt.level))
		})
	}
}

func TestTransactionOptions_Defaults(t *testing.T) {
	opts := &TransactionOptions{
		IsolationLevel: ReadCommitted,
		Timeout:        30 * time.Second,
	}

	assert.Equal(t, ReadCommitted, opts.IsolationLevel)
	assert.Equal(t, 30*time.Second, opts.Timeout)
}

// Integration tests - these require a running ThemisDB server
// Run with: go test -tags=integration

func TestIntegration_BasicOperations(t *testing.T) {
	t.Skip("Integration test - requires running ThemisDB server")

	client := NewClient(Config{
		Endpoints: []string{"http://localhost:8080"},
	})
	ctx := context.Background()

	// Test Put
	data := map[string]interface{}{
		"name":  "Alice",
		"email": "alice@example.com",
	}
	err := client.Put(ctx, "relational", "users", "test-user-1", data)
	require.NoError(t, err)

	// Test Get
	var result map[string]interface{}
	err = client.Get(ctx, "relational", "users", "test-user-1", &result)
	require.NoError(t, err)
	assert.Equal(t, "Alice", result["name"])

	// Test Delete
	err = client.Delete(ctx, "relational", "users", "test-user-1")
	require.NoError(t, err)
}

func TestIntegration_Transaction(t *testing.T) {
	t.Skip("Integration test - requires running ThemisDB server")

	client := NewClient(Config{
		Endpoints: []string{"http://localhost:8080"},
	})
	ctx := context.Background()

	// Begin transaction
	tx, err := client.BeginTransaction(ctx, &TransactionOptions{
		IsolationLevel: Snapshot,
	})
	require.NoError(t, err)
	require.NotNil(t, tx)
	assert.True(t, tx.IsActive())

	// Put data in transaction
	data := map[string]interface{}{
		"name":    "Bob",
		"balance": 1000,
	}
	err = tx.Put(ctx, "relational", "accounts", "acc-1", data)
	require.NoError(t, err)

	// Get data in transaction
	var result map[string]interface{}
	err = tx.Get(ctx, "relational", "accounts", "acc-1", &result)
	require.NoError(t, err)
	assert.Equal(t, "Bob", result["name"])

	// Commit transaction
	err = tx.Commit(ctx)
	require.NoError(t, err)
	assert.False(t, tx.IsActive())
}

func TestIntegration_TransactionRollback(t *testing.T) {
	t.Skip("Integration test - requires running ThemisDB server")

	client := NewClient(Config{
		Endpoints: []string{"http://localhost:8080"},
	})
	ctx := context.Background()

	// Begin transaction
	tx, err := client.BeginTransaction(ctx, nil)
	require.NoError(t, err)

	// Put data
	data := map[string]interface{}{"name": "Charlie"}
	err = tx.Put(ctx, "relational", "users", "test-user-2", data)
	require.NoError(t, err)

	// Rollback
	err = tx.Rollback(ctx)
	require.NoError(t, err)
	assert.False(t, tx.IsActive())

	// Verify data was not committed
	var result map[string]interface{}
	err = client.Get(ctx, "relational", "users", "test-user-2", &result)
	assert.Error(t, err) // Should not exist
}
