#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <utility>

namespace themis {

class RocksDBWrapper;
class SecondaryIndexManager;
class BaseEntity;

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

class QueryEngine {
public:
    struct Status {
        bool ok = true;
        std::string message;
        static Status OK() { return {}; }
        static Status Error(std::string msg) { return Status{false, std::move(msg)}; }
    };

    QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx);

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

private:
    RocksDBWrapper& db_;
    SecondaryIndexManager& secIdx_;

    static std::vector<std::string> intersectSortedLists_(std::vector<std::vector<std::string>> lists);
    static std::vector<std::string> unionSortedLists_(std::vector<std::vector<std::string>> lists);

    // Full-Scan Fallback: Durchsucht alle Reihen einer Tabelle und filtert per Prädikaten
    std::vector<std::string> fullScanAndFilter_(const ConjunctiveQuery& q) const;

    // Range-Unterstützung
    std::pair<Status, std::vector<std::string>> executeAndKeysRangeAware_(const ConjunctiveQuery& q) const;
    std::pair<Status, std::vector<BaseEntity>> executeAndEntitiesRangeAware_(const ConjunctiveQuery& q) const;
};

} // namespace themis
