#pragma once

#include <string>
#include <utility>
#include <nlohmann/json.hpp>
#include "query/aql_parser.h"
#include "query/aql_translator.h"
#include "query_engine.h"

namespace themis {

// High-level convenience dispatcher for AQL execution.
// Translates AQL to internal query forms and invokes the proper QueryEngine method.
// Returns Status + JSON payload for uniform downstream handling.
std::pair<QueryEngine::Status, nlohmann::json> executeAql(const std::string& aql, QueryEngine& engine);

} // namespace themis
