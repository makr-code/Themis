#pragma once

#include "storage/rocksdb_wrapper.h"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <utility>

namespace themis {

class BaseEntity;

/// VectorIndexManager
/// - Optional HNSWlib-Unterstützung (compile-time)
/// - Fallback: Brute-Force (L2/Cosine) über in-memory Cache oder RocksDB-Scan
/// - Persistenz: Vektoren liegen in RocksDB unter Namespace objectName:pk als BaseEntity
/// - Atomare Operationen via WriteBatch (analog zu Secondary/Graph-Indizes)
/// - In-Memory Cache für schnellen Zugriff, optional HNSW-Index für ANN
class VectorIndexManager {
public:
    enum class Metric { L2, COSINE };

    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    struct Result {
        std::string pk;
        float distance = 0.0f; // kleiner = besser (für COSINE: 1 - cosine)
    };

    explicit VectorIndexManager(RocksDBWrapper& db);
    ~VectorIndexManager();

    // Initialisierung eines Index-Namespace (z. B. "documents"): Dimension, M/ef, Metrik
    Status init(std::string_view objectName, int dim, Metric metric = Metric::COSINE,
                int M = 16, int efConstruction = 200, int efSearch = 64,
                const std::string& savePath = "");

    // Lifecycle-Management
        void setAutoSavePath(const std::string& savePath, bool autoSave = true);
    Status shutdown(); // Speichert Index wenn auto_save aktiviert

    // HNSW Parameter zur Laufzeit anpassen (nur efSearch; M/efConstruction erfordern Rebuild)
    Status setEfSearch(int efSearch);

    // Index aus Storage aufbauen (scannt Prefix objectName:) — optional
    Status rebuildFromStorage();

    // Persistenz (optional, nur wenn HNSW aktiv): speichert Index + Mapping + Metadaten im Verzeichnis
    Status saveIndex(const std::string& directory) const;
    Status loadIndex(const std::string& directory);

    // CRUD (Standard: direktes Commit)
    Status addEntity(const BaseEntity& e, std::string_view vectorField = "embedding");
    Status updateEntity(const BaseEntity& e, std::string_view vectorField = "embedding");
    Status removeByPk(std::string_view pk);
    
    // CRUD für Transaktionen: nutzen bestehende WriteBatch
    Status addEntity(const BaseEntity& e, RocksDBWrapper::WriteBatchWrapper& batch, 
                     std::string_view vectorField = "embedding");
    Status updateEntity(const BaseEntity& e, RocksDBWrapper::WriteBatchWrapper& batch,
                        std::string_view vectorField = "embedding");
    Status removeByPk(std::string_view pk, RocksDBWrapper::WriteBatchWrapper& batch);

    // MVCC Transaction Varianten
    Status addEntity(const BaseEntity& e, RocksDBWrapper::TransactionWrapper& txn,
                     std::string_view vectorField = "embedding");
    Status updateEntity(const BaseEntity& e, RocksDBWrapper::TransactionWrapper& txn,
                        std::string_view vectorField = "embedding");
    Status removeByPk(std::string_view pk, RocksDBWrapper::TransactionWrapper& txn);

    // KNN-Suche; optional Whitelist von PKs für hybrides Pre-Filtering
    std::pair<Status, std::vector<Result>> searchKnn(
        const std::vector<float>& query,
        size_t k,
        const std::vector<std::string>* whitelistPks = nullptr
    ) const;

    // Getter für Konfiguration & Statistiken
    std::string getObjectName() const { return objectName_; }
    int getDimension() const { return dim_; }
    Metric getMetric() const { return metric_; }
    int getEfSearch() const { return efSearch_; }
    int getM() const { return m_; }
    int getEfConstruction() const { return efConstruction_; }
    size_t getVectorCount() const { return pkToId_.size(); }
    bool isHnswEnabled() const { return useHnsw_; }
        std::string getSavePath() const { return savePath_; }

private:
    RocksDBWrapper& db_;
    std::string objectName_;
    int dim_ = 0;
    Metric metric_ = Metric::COSINE;
    int efSearch_ = 64;
    int m_ = 16;
    int efConstruction_ = 200;
        std::string savePath_; // Verzeichnis für saveIndex/loadIndex
        bool autoSave_ = false; // Automatisches Speichern bei shutdown()

    // In-Memory Mapping PK <-> Label-ID (für HNSW) und Cache für Fallback
    mutable std::unordered_map<std::string, size_t> pkToId_;
    mutable std::vector<std::string> idToPk_;
    mutable std::unordered_map<std::string, std::vector<float>> cache_; // für Fallback/Whitelist

    // HNSWlib Index (wenn verfügbar)
#ifdef THEMIS_HNSW_ENABLED
    struct HnswDeleter { void operator()(void* /*p*/) const {} };
    // Wir verwenden Pointer-void, um hnswlib-Header-Dependency zu vermeiden, wenn nicht definiert
    void* hnswIndex_ = nullptr; // tatsächlich hnswlib::HierarchicalNSW<float>*
    bool useHnsw_ = false;
#else
    bool useHnsw_ = false;
#endif

    // Hilfsfunktionen
    static float l2(const std::vector<float>& a, const std::vector<float>& b);
    static float cosineOneMinus(const std::vector<float>& a, const std::vector<float>& b);
    static void normalizeL2(std::vector<float>& v);
    float distance(const std::vector<float>& a, const std::vector<float>& b) const;

    // Storage Keys
    std::string makeObjectKey(std::string_view pk) const;

    // Interne Suche
    std::vector<Result> bruteForceSearch_(const std::vector<float>& query, size_t k,
                                          const std::vector<std::string>* whitelist) const;
};

} // namespace themis
