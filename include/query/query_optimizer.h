#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "query/query_engine.h"

namespace themis {

class SecondaryIndexManager;

class QueryOptimizer {
public:
    struct Estimation {
        PredicateEq pred;
        size_t estimatedCount = 0; // bis maxProbe gezählt
        bool capped = false;       // true, wenn abgeschnitten (>= maxProbe)
    };

    struct Plan {
        std::vector<PredicateEq> orderedPredicates; // aufsteigend nach erwarteter Selektivität
        std::vector<Estimation> details;            // für Logging/Diagnose
    };

    QueryOptimizer(SecondaryIndexManager& secIdx);

    // Schätzt Selektivitäten der Gleichheitsprädikate und liefert eine Ordnung (kleinste zuerst)
    Plan chooseOrderForAndQuery(const ConjunctiveQuery& q, size_t maxProbePerPred = 1000) const;

    // Führt die Anfrage mit der geplanten Reihenfolge aus (sequenziell)
    std::pair<QueryEngine::Status, std::vector<std::string>>
    executeOptimizedKeys(QueryEngine& engine, const ConjunctiveQuery& q, const Plan& plan) const;

    std::pair<QueryEngine::Status, std::vector<BaseEntity>>
    executeOptimizedEntities(QueryEngine& engine, const ConjunctiveQuery& q, const Plan& plan) const;

private:
    SecondaryIndexManager& secIdx_;
};

} // namespace themis
