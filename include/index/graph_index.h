#pragma once

#include "storage/rocksdb_wrapper.h"
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <functional>

namespace themis {

class BaseEntity;

/// GraphIndexManager
/// - Verwaltet Adjazenz-Indizes für gerichtete Kanten
/// - Key-Schema:
///   - Out: graph:out:<from_pk>:<edge_id>  -> value: <to_pk>
///   - In:  graph:in:<to_pk>:<edge_id>    -> value: <from_pk>
/// - Atomare Operationen via WriteBatch
/// - In-Memory Topologie für O(1) Nachbarschaftsabfragen
/// - Saubere Fehler über Status-Rückgabe, kein Exception-API nach außen
class GraphIndexManager {
public:
    struct AdjacencyInfo {
        std::string edgeId;
        std::string targetPk;
    };

    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    explicit GraphIndexManager(RocksDBWrapper& db);

    // Topologie aus RocksDB laden (optional beim Start)
    Status rebuildTopology();

    // Kanten-Operationen (Edge-Entity erwartet Felder: id, _from, _to)
    Status addEdge(const BaseEntity& edge);
    Status deleteEdge(std::string_view edgeId);

    // Varianten für Transaktionen: nutzen bestehende WriteBatch
    Status addEdge(const BaseEntity& edge, RocksDBWrapper::WriteBatchWrapper& batch);
    Status deleteEdge(std::string_view edgeId, RocksDBWrapper::WriteBatchWrapper& batch);

    // MVCC Transaction Varianten
    Status addEdge(const BaseEntity& edge, RocksDBWrapper::TransactionWrapper& txn);
    Status deleteEdge(std::string_view edgeId, RocksDBWrapper::TransactionWrapper& txn);

    // Nachbarschaftsabfragen (nutzt In-Memory falls verfügbar, sonst RocksDB)
    std::pair<Status, std::vector<std::string>> outNeighbors(std::string_view fromPk) const;
    std::pair<Status, std::vector<std::string>> inNeighbors(std::string_view toPk) const;

    // Nachbarschaft mit Kanten-IDs (für RETURN e/p)
    std::pair<Status, std::vector<AdjacencyInfo>> outAdjacency(std::string_view fromPk) const;
    std::pair<Status, std::vector<AdjacencyInfo>> inAdjacency(std::string_view toPk) const;

    // Traversierungen
    std::pair<Status, std::vector<std::string>> bfs(
        std::string_view startPk,
        int maxDepth = 3
    ) const;

    // Shortest-Path-Algorithmen (gewichtete Graphen)
    // Weight wird aus Edge-Entity-Feld "_weight" gelesen (default: 1.0)
    struct PathResult {
        std::vector<std::string> path;  // Knoten vom Start zum Ziel
        double totalCost = 0.0;
    };

    // Dijkstra: Kürzester Pfad von start zu target
    std::pair<Status, PathResult> dijkstra(
        std::string_view startPk,
        std::string_view targetPk
    ) const;

    // A*: Kürzester Pfad mit Heuristik (optional)
    // heuristic: Funktion die Schätzkosten von einem Knoten zum Ziel liefert
    using HeuristicFunc = std::function<double(const std::string& node)>;
    std::pair<Status, PathResult> aStar(
        std::string_view startPk,
        std::string_view targetPk,
        HeuristicFunc heuristic = nullptr
    ) const;

    // Statistiken
    size_t getTopologyNodeCount() const;
    size_t getTopologyEdgeCount() const;

private:
    RocksDBWrapper& db_;

    // In-Memory Adjazenzlisten (thread-safe)
    mutable std::mutex topology_mutex_;
    std::unordered_map<std::string, std::vector<AdjacencyInfo>> outEdges_; // fromPk -> [(edgeId, toPk)]
    std::unordered_map<std::string, std::vector<AdjacencyInfo>> inEdges_;  // toPk -> [(edgeId, fromPk)]
    bool topologyLoaded_ = false;

    // Hilfsfunktionen
    void addEdgeToTopology_(const std::string& edgeId, const std::string& from, const std::string& to);
    void removeEdgeFromTopology_(const std::string& edgeId, const std::string& from, const std::string& to);
    
    // Edge-Weight-Parsing (liest _weight aus Edge-Entity, default 1.0)
    double getEdgeWeight_(std::string_view edgeId) const;
    
    static std::vector<uint8_t> toBytes(std::string_view sv);
};

} // namespace themis
