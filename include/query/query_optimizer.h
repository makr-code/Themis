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

    // =============================
    // Hybrid Vector+Geo Cost Model
    // =============================
    struct VectorGeoCostInput {
        bool hasVectorIndex = false;
        bool hasSpatialIndex = false;
        double bboxRatio = 1.0;              // area(bbox)/area(total)
        size_t prefilterSize = 0;             // equality prefilter candidate universe
        size_t spatialIndexEntries = 0;       // number of spatial index entries (approx table size for spatial filter)
        size_t k = 10;                        // requested top-k
        size_t vectorDim = 0;                 // vector dimension (for scaling)
        size_t overfetch = 1;                 // overfetch multiplier
    };
    enum class VectorGeoPlan { SpatialThenVector, VectorThenSpatial };
    struct VectorGeoCostResult {
        VectorGeoPlan plan;
        double costSpatialFirst;
        double costVectorFirst;
    };
    static VectorGeoCostResult chooseVectorGeoPlan(const VectorGeoCostInput& in);

    // =============================
    // Content+Geo (Fulltext + Spatial) Cost Model (stub)
    // =============================
    struct ContentGeoCostInput {
        bool hasFulltextIndex = true;
        bool hasSpatialIndex = false;
        size_t fulltextHits = 0;          // estimated FT hit count
        double bboxRatio = 1.0;           // spatial selectivity
        size_t limit = 100;               // requested limit
    };
    struct ContentGeoCostResult {
        double costFulltextThenSpatial;
        double costSpatialThenFulltext; // for future when spatial prefilter can restrict FT search scope
        bool chooseFulltextFirst;       // current plan choice
    };
    static ContentGeoCostResult estimateContentGeo(const ContentGeoCostInput& in);

    // =============================
    // Graph Shortest Path Cost Model (stub)
    // =============================
    struct GraphPathCostInput {
        size_t maxDepth = 5;
        size_t branchingFactor = 4;    // estimated average outgoing edges per vertex
        bool hasSpatialConstraint = false;
        double spatialSelectivity = 1.0; // fraction of vertices passing spatial filter
    };
    struct GraphPathCostResult {
        double estimatedExpandedVertices;
        double estimatedTimeMs; // abstract
    };
    static GraphPathCostResult estimateGraphPath(const GraphPathCostInput& in);

private:
    SecondaryIndexManager& secIdx_;
};

} // namespace themis
