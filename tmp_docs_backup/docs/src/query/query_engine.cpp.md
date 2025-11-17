# query_engine.cpp

Path: `src/query/query_engine.cpp`

Purpose: Executes query plans, integrates BM25 scoring, vector search fusion, and result ranking.

Public functions / symbols:
- `if (!st.ok) {`
- `for (const auto& res : results) {`
- `if (!structStatus.ok) {`
- `for (const auto& pk : keys) {`
- `catch (...) { THEMIS_WARN("executeAndEntities: Deserialisierung fehlgeschlagen für PK={}", pk); }`
- ``
- `for (auto& batch : batches) {`
- `catch (...) { THEMIS_WARN("executeOrEntitiesWithFallback: Deserialisierung fehlgeschlagen für PK={}", pk); }`
- `for (size_t i = start; i < end; ++i) {`
- `catch (...) { THEMIS_WARN("executeOrEntities: Deserialisierung fehlgeschlagen für PK={}", pk); }`
- `if (!st0.ok) { child.setStatus(false, st0.message); return {Status::Error("sequential: " + st0.message), {}}; }`
- `if (!st.ok) { child2.setStatus(false, st.message); return {Status::Error("sequential: " + st.message), {}}; }`
- `catch (...) { THEMIS_WARN("executeAndEntitiesSequential: Deserialisierung fehlgeschlagen für PK={}", pk); }`
- `catch (...) { /* Silent failure in parallel context */ }`
- `for (const auto& p : q.predicates) {`
- `if (!v || *v != p.value) { match = false; break; }`
- `if (match) {`
- `for (const auto& r : q.rangePredicates) {`
- `if (!v) { match = false; break; }`
- `if (eff < bestEst) { bestEst = eff; bestIdx = i; bestCapped = capped; }`
- `if (!st.ok) { span.setStatus(false, st.message); return {st, {}}; }`
- `if (optimize) {`
- `catch (...) { THEMIS_WARN("executeAndEntitiesWithFallback: Deserialisierung fehlgeschlagen für PK={}", pk); }`
- `static inline size_t bigLimit() { return static_cast<size_t>(1000000000ULL); }`
- `if (r.column == ob.column) { lb = r.lower; ub = r.upper; il = r.includeLower; iu = r.includeUpper; break; }`
- `for (const auto& k : scan) {`
- `catch (...) { THEMIS_WARN("executeAndEntitiesRangeAware_: Deserialisierung fehlgeschlagen für PK={}", pk); }`
- `if constexpr (std::is_same_v<T, std::nullptr_t>) {`
- `for (const auto& [key, valExpr] : objConst->fields) {`
- `for (const auto& elemExpr : arrLit->elements) {`
- `if (name == "bm25") {`
- `if (name == "fulltext_score") {`
- `static void collectVariables(`
- `for (const auto& arg : fn->arguments) {`
- `for (const auto& elem : arr->elements) {`
- `for (const auto& [key, val] : obj->fields) {`
- `if (equiJoin.found) {`
- `for (const auto& filter : build_filters->second) {`
- `for (const auto& filter : probe_filters->second) {`
- `for (const auto& build_doc : it->second) {`
- `for (const auto& let : let_nodes) {`
- `for (const auto& filter : multi_var_filters) {`
- `if (bin->op == query::BinaryOperator::Eq) {`
- `if (return_node) {`
- `for (const auto& filter : push_filters->second) {`
- `for (const auto& filter : filters) {`
- `for (const auto& agg : collect->aggregations) {`
- `for (const auto& doc : docs) {`
- `for (const auto& node : reachableNodes) {`
- `if (node != q.start_node) {`

Notes / TODOs:
- Document scoring fusion, BM25 parameterization, and how vector indices are invoked.
