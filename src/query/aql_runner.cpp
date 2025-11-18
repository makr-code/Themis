#include "query/aql_runner.h"
#include "storage/base_entity.h"

namespace themis {

std::pair<QueryEngine::Status, nlohmann::json> executeAql(const std::string& aql, QueryEngine& engine) {
    query::AQLParser parser;
    auto parseResult = parser.parse(aql);
    if (!parseResult.success) {
        return { QueryEngine::Status::Error(parseResult.error.toString()), nlohmann::json{{"error","parse"}} };
    }
    auto tr = AQLTranslator::translate(parseResult.query);
    if (!tr.success) {
        return { QueryEngine::Status::Error(tr.error_message), nlohmann::json{{"error","translate"}} };
    }

    // Vector+Geo hybrid dispatch
    if (tr.vector_geo.has_value()) {
        auto [st, res] = engine.executeVectorGeoQuery(*tr.vector_geo);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : res) {
            arr.push_back({
                {"pk", r.pk},
                {"distance", r.vector_distance},
                {"entity", r.entity}
            });
        }
        return { st, nlohmann::json{{"type","vector_geo"},{"results", arr}} };
    }
    // Content+Geo hybrid dispatch (FULLTEXT + PROXIMITY)
    if (tr.content_geo.has_value()) {
        auto [st, res] = engine.executeContentGeoQuery(*tr.content_geo);
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& r : res) {
            nlohmann::json row = {
                {"pk", r.pk},
                {"bm25", r.bm25_score},
                {"entity", r.entity}
            };
            if (r.geo_distance.has_value()) row["geo_distance"] = *r.geo_distance;
            arr.push_back(std::move(row));
        }
        return { st, nlohmann::json{{"type","content_geo"},{"results", arr}} };
    }

    // Disjunctive OR query
    if (tr.disjunctive.has_value()) {
        auto [st, ents] = engine.executeOrEntitiesWithFallback(*tr.disjunctive, true);
        nlohmann::json arr = nlohmann::json::array();
        for (auto& e : ents) {
            arr.push_back(nlohmann::json::parse(e.toJson()));
        }
        return { st, nlohmann::json{{"type","or"},{"results", arr}} };
    }

    // Traversal / Shortest Path dispatch
    if (tr.traversal.has_value()) {
        const auto &tv = *tr.traversal;
        if (tv.shortestPath) {
            RecursivePathQuery rq; rq.start_node = tv.startVertex; rq.end_node = tv.endVertex; rq.graph_id = tv.graphName; rq.max_depth = tv.maxDepth; rq.edge_type = ""; // edge_type placeholder
            auto [st, paths] = engine.executeRecursivePathQuery(rq);
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& p : paths) arr.push_back(p);
            return { st, nlohmann::json{{"type","shortest_path"},{"paths", arr}} };
        }
        return { QueryEngine::Status::Error("Traversal dispatch (non-shortest) not implemented"), nlohmann::json{{"error","traversal_not_implemented"}} };
    }

    // Join query
    if (tr.join.has_value()) {
        auto& j = *tr.join;
        auto [st, rows] = engine.executeJoin(j.for_nodes, j.filters, j.let_nodes, j.return_node, j.sort, j.limit);
        return { st, nlohmann::json{{"type","join"},{"results", rows}} };
    }

    // Conjunctive (default) query
    auto [st, entities] = engine.executeAndEntitiesWithFallback(tr.query, true);
    nlohmann::json arr = nlohmann::json::array();
    for (auto& e : entities) arr.push_back(nlohmann::json::parse(e.toJson()));
    return { st, nlohmann::json{{"type","and"},{"results", arr}} };
}

} // namespace themis
