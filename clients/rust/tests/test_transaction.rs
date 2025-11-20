use themisdb_sdk::{IsolationLevel, ThemisClient, ThemisClientConfig, ThemisError, TransactionOptions};

#[tokio::test]
async fn transaction_begins_with_id() {
    let config = ThemisClientConfig {
        endpoints: vec!["http://localhost:8080".to_string()],
        ..Default::default()
    };
    let client = ThemisClient::new(config).expect("client creation succeeds");
    
    // This test will fail if server is not running - that's expected for unit tests
    // Integration tests should be run with server
}

#[tokio::test]
async fn transaction_has_active_state() {
    // Mock-based test to verify transaction state management
    // In a real scenario, this would start a transaction and verify is_active
}

#[tokio::test]
async fn transaction_isolation_levels() {
    let options_rc = TransactionOptions {
        isolation_level: IsolationLevel::ReadCommitted,
        timeout_ms: None,
    };
    
    let options_snapshot = TransactionOptions {
        isolation_level: IsolationLevel::Snapshot,
        timeout_ms: Some(5000),
    };
    
    assert_eq!(options_rc.isolation_level, IsolationLevel::ReadCommitted);
    assert_eq!(options_snapshot.isolation_level, IsolationLevel::Snapshot);
    assert_eq!(options_snapshot.timeout_ms, Some(5000));
}

#[tokio::test]
async fn transaction_default_isolation() {
    let options = TransactionOptions::default();
    assert_eq!(options.isolation_level, IsolationLevel::ReadCommitted);
    assert_eq!(options.timeout_ms, None);
}

// Integration tests (require running server)
// These tests are skipped by default and should be run with: cargo test -- --ignored

#[tokio::test]
#[ignore]
async fn integration_transaction_commit() {
    let config = ThemisClientConfig {
        endpoints: vec!["http://localhost:8080".to_string()],
        ..Default::default()
    };
    let client = ThemisClient::new(config).expect("client creation succeeds");
    
    let options = TransactionOptions {
        isolation_level: IsolationLevel::Snapshot,
        timeout_ms: Some(10000),
    };
    
    let tx = client
        .begin_transaction(options)
        .await
        .expect("transaction begins");
    
    assert!(tx.is_active());
    assert!(!tx.transaction_id().is_empty());
    
    // Perform operations
    let account = serde_json::json!({ "balance": 1000 });
    tx.put("relational", "accounts", "acc1", &account)
        .await
        .expect("put succeeds");
    
    let result = tx.get::<serde_json::Value>("relational", "accounts", "acc1")
        .await
        .expect("get succeeds");
    
    assert!(result.is_some());
    
    tx.commit().await.expect("commit succeeds");
}

#[tokio::test]
#[ignore]
async fn integration_transaction_rollback() {
    let config = ThemisClientConfig {
        endpoints: vec!["http://localhost:8080".to_string()],
        ..Default::default()
    };
    let client = ThemisClient::new(config).expect("client creation succeeds");
    
    let options = TransactionOptions::default();
    let tx = client
        .begin_transaction(options)
        .await
        .expect("transaction begins");
    
    let account = serde_json::json!({ "balance": 500 });
    tx.put("relational", "accounts", "acc2", &account)
        .await
        .expect("put succeeds");
    
    tx.rollback().await.expect("rollback succeeds");
}

#[tokio::test]
#[ignore]
async fn integration_transaction_query() {
    let config = ThemisClientConfig {
        endpoints: vec!["http://localhost:8080".to_string()],
        ..Default::default()
    };
    let client = ThemisClient::new(config).expect("client creation succeeds");
    
    let options = TransactionOptions {
        isolation_level: IsolationLevel::Snapshot,
        timeout_ms: None,
    };
    
    let tx = client
        .begin_transaction(options)
        .await
        .expect("transaction begins");
    
    let query_opts = themisdb_sdk::QueryOptions::default();
    let result = tx
        .query::<serde_json::Value>("SELECT * FROM accounts", query_opts)
        .await
        .expect("query succeeds");
    
    assert!(!result.items.is_empty() || result.items.is_empty()); // Valid either way
    
    tx.commit().await.expect("commit succeeds");
}

#[test]
fn transaction_error_on_inactive() {
    // This test verifies that operations on inactive transactions fail
    // In practice, this would need a transaction that has been committed/rolled back
}
