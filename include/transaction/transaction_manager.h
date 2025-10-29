#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include "storage/rocksdb_wrapper.h"

namespace themis {

class BaseEntity;
class SecondaryIndexManager;
class GraphIndexManager;
class VectorIndexManager;
class Saga;

/// Isolation levels for transactions
enum class IsolationLevel {
    ReadCommitted,  // Default: only committed data visible
    Snapshot        // Snapshot isolation (point-in-time consistency)
};

/// TransactionManager: ACID-ähnliche, atomare Multi-Layer-Updates via RocksDB WriteBatch
class TransactionManager {
public:
    using TransactionId = uint64_t;
    
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    explicit TransactionManager(RocksDBWrapper& db,
                                SecondaryIndexManager& secIdx,
                                GraphIndexManager& graphIdx,
                                VectorIndexManager& vecIdx);

    class Transaction {
    public:
        Transaction(TransactionId id,
                    RocksDBWrapper& db,
                    SecondaryIndexManager& secIdx,
                    GraphIndexManager& graphIdx,
                    VectorIndexManager& vecIdx,
                    IsolationLevel isolation);
        ~Transaction();

        // Keine Kopie, aber Move
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
        Transaction(Transaction&&) noexcept;
        Transaction& operator=(Transaction&&) noexcept;
        
        // Transaction metadata
        TransactionId getId() const { return id_; }
        IsolationLevel getIsolationLevel() const { return isolation_; }
        std::chrono::system_clock::time_point getStartTime() const { return start_time_; }
        uint64_t getDurationMs() const;
        bool isFinished() const { return finished_; }

        // Relational
        Status putEntity(std::string_view table, const BaseEntity& entity);
        Status eraseEntity(std::string_view table, std::string_view pk);

        // Graph
        Status addEdge(const BaseEntity& edgeEntity);
        Status deleteEdge(std::string_view edgeId);
        
        // Vector
        Status addVector(const BaseEntity& entity, std::string_view vectorField = "embedding");
        Status updateVector(const BaseEntity& entity, std::string_view vectorField = "embedding");
        Status removeVector(std::string_view pk);

        // Abschluss
        Status commit();
        void rollback();
        
        // SAGA support
        Saga& getSaga() { return *saga_; }
        const Saga& getSaga() const { return *saga_; }

    private:
        TransactionId id_;
        RocksDBWrapper& db_;
        SecondaryIndexManager& secIdx_;
        GraphIndexManager& graphIdx_;
        VectorIndexManager& vecIdx_;
        IsolationLevel isolation_;
        std::chrono::system_clock::time_point start_time_;
        std::unique_ptr<class RocksDBWrapper::TransactionWrapper> mvcc_txn_; // MVCC Transaction
        std::unique_ptr<Saga> saga_; // SAGA pattern for compensating actions
        bool finished_ = false;
    };

    // Session-based transaction management
    TransactionId beginTransaction(IsolationLevel isolation = IsolationLevel::ReadCommitted);
    std::shared_ptr<Transaction> getTransaction(TransactionId id);
    Status commitTransaction(TransactionId id);
    void rollbackTransaction(TransactionId id);
    
    // Direct transaction (legacy API)
    Transaction begin(IsolationLevel isolation = IsolationLevel::ReadCommitted);
    
    // Statistics
    struct Stats {
        uint64_t total_begun;
        uint64_t total_committed;
        uint64_t total_aborted;
        uint64_t active_count;
        uint64_t avg_duration_ms;
        uint64_t max_duration_ms;
    };
    Stats getStats() const;
    
    // Cleanup old completed transactions (after 1 hour by default)
    void cleanupOldTransactions(std::chrono::seconds max_age = std::chrono::hours(1));

private:
    RocksDBWrapper& db_;
    SecondaryIndexManager& secIdx_;
    GraphIndexManager& graphIdx_;
    VectorIndexManager& vecIdx_;
    
    // Session management
    mutable std::mutex sessions_mutex_;
    std::unordered_map<TransactionId, std::shared_ptr<Transaction>> active_transactions_;
    std::unordered_map<TransactionId, std::shared_ptr<Transaction>> completed_transactions_;
    
    // Transaction ID generator
    std::atomic<uint64_t> next_transaction_id_{1};
    
    // Statistics
    std::atomic<uint64_t> total_begun_{0};
    std::atomic<uint64_t> total_committed_{0};
    std::atomic<uint64_t> total_aborted_{0};
    
    TransactionId generateTransactionId();
    void moveToCompleted(TransactionId id);
};

} // namespace themis
