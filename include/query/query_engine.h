﻿#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <utility>
#include <unordered_map>
#include <memory>
#include <nlohmann/json.hpp>

namespace themis {

struct RecursivePathQuery {
    std::string start_node;
    std::string end_node;
    std::string edge_type;
    size_t max_depth = 5;
    std::optional<std::string> valid_from;
    std::optional<std::string> valid_to;
    // Optional: weitere Filter auf Knoten/Kanten
};

class RocksDBWrapper;
class SecondaryIndexManager;
class BaseEntity;

// Forward declarations für AQL-Typen
namespace query {
    struct ForNode;
    struct FilterNode;
    struct LetNode;
    struct ReturnNode;
    struct SortNode;
    struct LimitNode;
    struct CollectNode;
    struct Expression;
}

struct PredicateEq {
    std::string column;
    std::string value; // bereits als String; Encoding für Indexkeys übernimmt SecondaryIndexManager
};

struct PredicateRange {
    std::string column;
    std::optional<std::string> lower; // gte
    std::optional<std::string> upper; // lte
    bool includeLower = true;
    bool includeUpper = true;
};

struct OrderBy {
    std::string column;
    bool desc = false;
    size_t limit = 1000;
    // Optional Cursor-Anker für effiziente Paginierung über Range-Indizes
    // Wenn gesetzt, startet der Scan strikt NACH dem Tupel (cursor_value, cursor_pk)
    // bei asc (desc=false) bzw. strikt VOR dem Tupel bei desc=true.
    std::optional<std::string> cursor_value; // Wert der Sortierspalte des letzten Elements
    std::optional<std::string> cursor_pk;    // PK des letzten Elements (Tiebreaker)
};

struct ConjunctiveQuery {
    std::string table;
    std::vector<PredicateEq> predicates; // alle mit AND verknüpft
    std::vector<PredicateRange> rangePredicates; // zusätzliche AND-Range-Prädikate
    std::optional<OrderBy> orderBy; // optionales ORDER BY über Range-Index
};

// Disjunctive Query: OR-verknüpfte AND-Blöcke (Disjunctive Normal Form)
// Beispiel: (city==Berlin AND age>18) OR (city==Munich AND age>21)
struct DisjunctiveQuery {
    std::string table;
    std::vector<ConjunctiveQuery> disjuncts; // OR-verknüpfte Conjunctions
    std::optional<OrderBy> orderBy;
};

class GraphIndexManager;

class QueryEngine {
public:
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx);
    QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx, GraphIndexManager& graphIdx);
    // Rekursive Pfadabfrage (Multi-Hop Traversal)
    std::pair<Status, std::vector<std::vector<std::string>>> executeRecursivePathQuery(const RecursivePathQuery& q) const;

    // Führt alle Gleichheitsprädikate parallel über Sekundärindizes aus und schneidet die PK-Mengen
    std::pair<Status, std::vector<BaseEntity>> executeAndEntities(const ConjunctiveQuery& q) const;
    std::pair<Status, std::vector<std::string>> executeAndKeys(const ConjunctiveQuery& q) const;

    // OR-Queries: Union von mehreren AND-Blöcken
    std::pair<Status, std::vector<std::string>> executeOrKeys(const DisjunctiveQuery& q) const;
    std::pair<Status, std::vector<BaseEntity>> executeOrEntities(const DisjunctiveQuery& q) const;

    // Sequenzielles Ausführen in vorgegebener Reihenfolge (z. B. vom Optimizer)
    std::pair<Status, std::vector<std::string>> executeAndKeysSequential(
        const std::string& table,
        const std::vector<PredicateEq>& orderedPredicates
    ) const;
    std::pair<Status, std::vector<BaseEntity>> executeAndEntitiesSequential(
        const std::string& table,
        const std::vector<PredicateEq>& orderedPredicates
    ) const;

    // Varianten mit Fallback (nutzen Full-Scan, wenn kein Index vorhanden ist)
    std::pair<Status, std::vector<std::string>> executeAndKeysWithFallback(
        const ConjunctiveQuery& q,
        bool optimize = true
    ) const;
    std::pair<Status, std::vector<BaseEntity>> executeAndEntitiesWithFallback(
        const ConjunctiveQuery& q,
        bool optimize = true
    ) const;
    
    // Join/LET/COLLECT Support (MVP) - Declared in cpp to avoid header dependency
    struct EvaluationContext;
    
    std::pair<Status, std::vector<nlohmann::json>> executeJoin(
        const std::vector<query::ForNode>& for_nodes,
        const std::vector<std::shared_ptr<query::FilterNode>>& filters,
        const std::vector<query::LetNode>& let_nodes,
        const std::shared_ptr<query::ReturnNode>& return_node,
        const std::shared_ptr<query::SortNode>& sort,
        const std::shared_ptr<query::LimitNode>& limit
    ) const;
    
    std::pair<Status, std::vector<nlohmann::json>> executeGroupBy(
        const query::ForNode& for_node,
        const std::shared_ptr<query::CollectNode>& collect,
        const std::vector<std::shared_ptr<query::FilterNode>>& filters,
        const std::shared_ptr<query::ReturnNode>& return_node
    ) const;

private:
    RocksDBWrapper& db_;
    SecondaryIndexManager& secIdx_;
    GraphIndexManager* graphIdx_ = nullptr; // Optional: für Graph-Queries
    
    // Expression evaluation helpers (implemented in cpp)
    nlohmann::json evaluateExpression(
        const std::shared_ptr<query::Expression>& expr,
        const EvaluationContext& ctx
    ) const;
    
    bool evaluateCondition(
        const std::shared_ptr<query::Expression>& expr,
        const EvaluationContext& ctx
    ) const;

    static std::vector<std::string> intersectSortedLists_(std::vector<std::vector<std::string>> lists);
    static std::vector<std::string> unionSortedLists_(std::vector<std::vector<std::string>> lists);

    // Full-Scan Fallback: Durchsucht alle Reihen einer Tabelle und filtert per Prädikaten
    std::vector<std::string> fullScanAndFilter_(const ConjunctiveQuery& q) const;

    // Range-Unterstützung
    std::pair<Status, std::vector<std::string>> executeAndKeysRangeAware_(const ConjunctiveQuery& q) const;
    std::pair<Status, std::vector<BaseEntity>> executeAndEntitiesRangeAware_(const ConjunctiveQuery& q) const;
};

// EvaluationContext Definition (moved from class body to avoid json dependency in header)
struct QueryEngine::EvaluationContext {
    std::unordered_map<std::string, nlohmann::json> bindings;
    
    void bind(const std::string& var, nlohmann::json value) {
        bindings[var] = std::move(value);
    }
    
    std::optional<nlohmann::json> get(const std::string& var) const {
        auto it = bindings.find(var);
        if (it != bindings.end()) return std::make_optional(it->second);
        return std::nullopt;
    }
};

} // namespace themis
