# transaction_manager.cpp

Path: `src/transaction/transaction_manager.cpp`

Purpose: Coordinates transactions, manages commit/rollback semantics and integrates with RocksDB transactions.

Public functions / symbols:
- `if (status.ok) {`
- ``
- `if (this != &other) {`
- `if (!st.ok) {`
- `if (!status.ok) {`
- `if (old_data) {`
- `if (finished_) {`
- `std::lock_guard<std::mutex> lock(sessions_mutex_);`
- `THEMIS_WARN("Transaction {} commit failed: {}", id, status.message);`
- `return Transaction(txn_id, db_, secIdx_, graphIdx_, vecIdx_, isolation);`
- `THEMIS_WARN("SAGA: Vector remove compensation failed for '{}': {}", pk, status.message);`
- `THEMIS_DEBUG("SAGA: Removing newly added vector for '{}'", pk);`
- `std::string pk_str(pk);`
- `THEMIS_ERROR("Transaction {} commit failed - MVCC conflict, executing SAGA compensation", id_);`
- `THEMIS_WARN("Transaction {} already finished, rollback skipped", id_);`

