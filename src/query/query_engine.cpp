// Parallel Query Engine implementation

#include "query/query_engine.h"
#include "query/query_optimizer.h"
#include "query/aql_parser.h"
#include "query/let_evaluator.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/spatial_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "storage/key_schema.h"
#include "utils/logger.h"
#include "utils/tracing.h"
#include <sstream>
#include <cmath>

#include <tbb/parallel_invoke.h>
#include <tbb/task_group.h>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <set>
#include <map>
#include <queue>

namespace themis {

QueryEngine::QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx)
	: db_(db), secIdx_(secIdx) {}

QueryEngine::QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx, GraphIndexManager& graphIdx)
	: db_(db), secIdx_(secIdx), graphIdx_(&graphIdx) {}

QueryEngine::QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx, GraphIndexManager& graphIdx,
                         VectorIndexManager* vectorIdx, SpatialIndexManager* spatialIdx)
	: db_(db), secIdx_(secIdx), graphIdx_(&graphIdx), vectorIdx_(vectorIdx), spatialIdx_(spatialIdx) {}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeys(const ConjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndKeys");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.eq_count", static_cast<int64_t>(q.predicates.size()));
	span.setAttribute("query.range_count", static_cast<int64_t>(q.rangePredicates.size()));
	span.setAttribute("query.order_by", q.orderBy.has_value());
	span.setAttribute("query.fulltext", q.fulltextPredicate.has_value());
	if (q.table.empty()) return {Status::Error("executeAndKeys: table darf nicht leer sein"), {}};
	
	// Handle fulltext queries
	if (q.fulltextPredicate.has_value()) {
		const auto& ft = *q.fulltextPredicate;
		auto child = Tracer::startSpan("index.scanFulltext");
		child.setAttribute("index.table", q.table);
		child.setAttribute("index.column", ft.column);
		child.setAttribute("index.query", ft.query);
		child.setAttribute("index.limit", static_cast<int64_t>(ft.limit));
		
		auto [st, results] = secIdx_.scanFulltextWithScores(q.table, ft.column, ft.query, ft.limit);
		if (!st.ok) {
			child.setStatus(false, st.message);
			return {Status::Error(st.message), {}};
		}
		
		// Extract PKs from results
		std::vector<std::string> fulltextKeys;
		fulltextKeys.reserve(results.size());
		for (const auto& res : results) {
			fulltextKeys.push_back(res.pk);
		}
		
		child.setAttribute("index.result_count", static_cast<int64_t>(fulltextKeys.size()));
		child.setStatus(true);
		
		// If there are additional predicates (AND combination), intersect with fulltext results
		if (!q.predicates.empty() || !q.rangePredicates.empty()) {
			auto intersectSpan = Tracer::startSpan("query.fulltext_and_intersection");
			intersectSpan.setAttribute("fulltext.result_count", static_cast<int64_t>(fulltextKeys.size()));
			intersectSpan.setAttribute("additional.eq_count", static_cast<int64_t>(q.predicates.size()));
			intersectSpan.setAttribute("additional.range_count", static_cast<int64_t>(q.rangePredicates.size()));
			
			// Create a temporary query with only the structural predicates
			ConjunctiveQuery structuralQuery;
			structuralQuery.table = q.table;
			structuralQuery.predicates = q.predicates;
			structuralQuery.rangePredicates = q.rangePredicates;
			structuralQuery.orderBy = q.orderBy;
			
			// Execute structural predicates
			auto [structStatus, structKeys] = executeAndKeysRangeAware_(structuralQuery);
			if (!structStatus.ok) {
				intersectSpan.setStatus(false, structStatus.message);
				return {structStatus, {}};
			}
			
			// Intersect fulltext results with structural predicate results
			// Both lists should be sorted for efficient intersection
			std::sort(fulltextKeys.begin(), fulltextKeys.end());
			std::sort(structKeys.begin(), structKeys.end());
			
			std::vector<std::string> intersection;
			std::set_intersection(
				fulltextKeys.begin(), fulltextKeys.end(),
				structKeys.begin(), structKeys.end(),
				std::back_inserter(intersection)
			);
			
			intersectSpan.setAttribute("intersection.result_count", static_cast<int64_t>(intersection.size()));
			intersectSpan.setStatus(true);
			span.setAttribute("query.result_count", static_cast<int64_t>(intersection.size()));
			span.setStatus(true);
			return {Status::OK(), std::move(intersection)};
		}
		
		// Standalone FULLTEXT (no additional predicates)
		span.setAttribute("query.result_count", static_cast<int64_t>(fulltextKeys.size()));
		span.setStatus(true);
		return {Status::OK(), std::move(fulltextKeys)};
	}
	
	// Erlaube ORDER BY ohne weitere Prädikate (liefert die ersten N gemäß Range-Index)
	if (q.predicates.empty() && q.rangePredicates.empty() && !q.orderBy.has_value()) {
		return {Status::Error("executeAndKeys: keine Prädikate"), {}};
	}

	// Wenn Range-Prädikate vorhanden sind, nutze die range-aware Logik (inkl. ORDER BY)
	if (!q.rangePredicates.empty() || q.orderBy.has_value()) {
		return executeAndKeysRangeAware_(q);
	}

	// Parallele Scans pro Prädikat
	std::vector<std::vector<std::string>> all_lists(q.predicates.size());
	std::vector<std::string> errors;
	tbb::task_group tg;

	for (size_t i = 0; i < q.predicates.size(); ++i) {
		const auto& p = q.predicates[i];
		tg.run([this, &q, &p, &all_lists, i, &errors]() {
			auto child = Tracer::startSpan("index.scanEqual");
			child.setAttribute("index.table", q.table);
			child.setAttribute("index.column", p.column);
			auto [st, keys] = secIdx_.scanKeysEqual(q.table, p.column, p.value);
			if (!st.ok) {
				THEMIS_ERROR("Parallel scan error ({}={}): {}", p.column, p.value, st.message);
				errors.push_back(st.message);
				child.setStatus(false, st.message);
				return;
			}
			// Sortieren zur späteren Schnittmenge
			std::sort(keys.begin(), keys.end());
			all_lists[i] = std::move(keys);
			child.setAttribute("index.result_count", static_cast<int64_t>(all_lists[i].size()));
			child.setStatus(true);
		});
	}
	tg.wait();

	if (!errors.empty()) {
		return {Status::Error("executeAndKeys: " + errors.front()), {}};
	}

	// Leere Listen früh abbrechen
	for (const auto& l : all_lists) {
		if (l.empty()) return {Status::OK(), {}};
	}

	// Schnittmenge bilden
	auto keys = intersectSortedLists_(std::move(all_lists));
	span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(keys)};
}

std::pair<QueryEngine::Status, QueryEngine::KeysWithScores>
QueryEngine::executeAndKeysWithScores(const ConjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndKeysWithScores");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.fulltext", q.fulltextPredicate.has_value());
	
	// If no FULLTEXT predicate, delegate to standard method (no scores)
	if (!q.fulltextPredicate.has_value()) {
		auto [st, keys] = executeAndKeys(q);
		KeysWithScores result;
		result.keys = std::move(keys);
		result.bm25_scores = std::make_shared<std::unordered_map<std::string, double>>();
		return {st, std::move(result)};
	}
	
	// FULLTEXT query: Extract scores
	const auto& ft = *q.fulltextPredicate;
	auto child = Tracer::startSpan("index.scanFulltextWithScores");
	child.setAttribute("index.table", q.table);
	child.setAttribute("index.column", ft.column);
	child.setAttribute("index.query", ft.query);
	child.setAttribute("index.limit", static_cast<int64_t>(ft.limit));
	
	auto [st, results] = secIdx_.scanFulltextWithScores(q.table, ft.column, ft.query, ft.limit);
	if (!st.ok) {
		child.setStatus(false, st.message);
		return {Status::Error(st.message), KeysWithScores{}};
	}
	
	// Build score map and key list
	auto scoreMap = std::make_shared<std::unordered_map<std::string, double>>();
	std::vector<std::string> fulltextKeys;
	fulltextKeys.reserve(results.size());
	scoreMap->reserve(results.size());
	
	for (const auto& res : results) {
		fulltextKeys.push_back(res.pk);
		(*scoreMap)[res.pk] = res.score;
	}
	
	child.setAttribute("index.result_count", static_cast<int64_t>(fulltextKeys.size()));
	child.setStatus(true);
	
	// If there are additional predicates (AND combination), intersect with fulltext results
	if (!q.predicates.empty() || !q.rangePredicates.empty()) {
		auto intersectSpan = Tracer::startSpan("query.fulltext_and_intersection");
		intersectSpan.setAttribute("fulltext.result_count", static_cast<int64_t>(fulltextKeys.size()));
		intersectSpan.setAttribute("additional.eq_count", static_cast<int64_t>(q.predicates.size()));
		intersectSpan.setAttribute("additional.range_count", static_cast<int64_t>(q.rangePredicates.size()));
		
		// Create a temporary query with only the structural predicates
		ConjunctiveQuery structuralQuery;
		structuralQuery.table = q.table;
		structuralQuery.predicates = q.predicates;
		structuralQuery.rangePredicates = q.rangePredicates;
		structuralQuery.orderBy = q.orderBy;
		
		// Execute structural predicates
		auto [structStatus, structKeys] = executeAndKeysRangeAware_(structuralQuery);
		if (!structStatus.ok) {
			intersectSpan.setStatus(false, structStatus.message);
			return {structStatus, KeysWithScores{}};
		}
		
		// Intersect fulltext results with structural predicate results
		std::sort(fulltextKeys.begin(), fulltextKeys.end());
		std::sort(structKeys.begin(), structKeys.end());
		
		std::vector<std::string> intersection;
		std::set_intersection(
			fulltextKeys.begin(), fulltextKeys.end(),
			structKeys.begin(), structKeys.end(),
			std::back_inserter(intersection)
		);
		
		// Filter score map to only include intersection keys
		auto filteredScores = std::make_shared<std::unordered_map<std::string, double>>();
		filteredScores->reserve(intersection.size());
		for (const auto& pk : intersection) {
			auto it = scoreMap->find(pk);
			if (it != scoreMap->end()) {
				(*filteredScores)[pk] = it->second;
			}
		}
		
		intersectSpan.setAttribute("intersection.result_count", static_cast<int64_t>(intersection.size()));
		intersectSpan.setStatus(true);
		span.setAttribute("query.result_count", static_cast<int64_t>(intersection.size()));
		span.setStatus(true);
		
		KeysWithScores result;
		result.keys = std::move(intersection);
		result.bm25_scores = std::move(filteredScores);
		return {Status::OK(), std::move(result)};
	}
	
	// Standalone FULLTEXT (no additional predicates)
	span.setAttribute("query.result_count", static_cast<int64_t>(fulltextKeys.size()));
	span.setStatus(true);
	
	KeysWithScores result;
	result.keys = std::move(fulltextKeys);
	result.bm25_scores = std::move(scoreMap);
	return {Status::OK(), std::move(result)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntities(const ConjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndEntities");
	span.setAttribute("query.table", q.table);
	auto [st, keys] = executeAndKeys(q);
	if (!st.ok) return {st, {}};

	// Paralleles Entity-Loading für große Ergebnismengen (Batch-Verarbeitung)
	constexpr size_t PARALLEL_THRESHOLD = 100;
	constexpr size_t BATCH_SIZE = 50;

	std::vector<BaseEntity> out;
	out.reserve(keys.size());

	if (keys.size() < PARALLEL_THRESHOLD) {
		// Sequential für kleine Mengen (weniger Overhead)
		for (const auto& pk : keys) {
			auto blob = db_.get(q.table + ":" + pk);
			if (!blob) continue;
			try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
			catch (...) { THEMIS_WARN("executeAndEntities: Deserialisierung fehlgeschlagen für PK={}", pk); }
		}
	} else {
		// Parallel für große Mengen: Batch-Processing mit TBB
		std::vector<std::vector<BaseEntity>> batches((keys.size() + BATCH_SIZE - 1) / BATCH_SIZE);
		tbb::task_group tg;

		for (size_t batch_idx = 0; batch_idx < batches.size(); ++batch_idx) {
			tg.run([this, &q, &keys, &batches, batch_idx, BATCH_SIZE]() {
				size_t start = batch_idx * BATCH_SIZE;
				size_t end = std::min(start + BATCH_SIZE, keys.size());
				std::vector<BaseEntity> local_entities;
				local_entities.reserve(end - start);

				for (size_t i = start; i < end; ++i) {
					const auto& pk = keys[i];
					auto blob = db_.get(q.table + ":" + pk);
					if (!blob) continue;
					try { local_entities.emplace_back(BaseEntity::deserialize(pk, *blob)); }
					catch (...) { THEMIS_WARN("executeAndEntities: Deserialisierung fehlgeschlagen für PK={}", pk); }
				}

				batches[batch_idx] = std::move(local_entities);
			});
		}
		tg.wait();

		// Merge batches
		for (auto& batch : batches) {
			out.insert(out.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
		}
	}
	span.setAttribute("query.entities_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(out)};
}

std::vector<std::string>
QueryEngine::intersectSortedLists_(std::vector<std::vector<std::string>> lists) {
	// Sortiere nach Größe, beginne mit kleinsten Listen für effiziente Schnittmenge
	std::sort(lists.begin(), lists.end(), [](const auto& a, const auto& b){ return a.size() < b.size(); });
	if (lists.empty()) return {};
	
	std::vector<std::string> result = lists.front();
	
	for (size_t i = 1; i < lists.size(); ++i) {
		const auto& next = lists[i];
		std::vector<std::string> tmp;
		tmp.reserve(std::min(result.size(), next.size()));
		std::set_intersection(result.begin(), result.end(), next.begin(), next.end(), std::back_inserter(tmp));
		result.swap(tmp);
		if (result.empty()) break;
	}
	return result;
}

std::vector<std::string>
QueryEngine::unionSortedLists_(std::vector<std::vector<std::string>> lists) {
	if (lists.empty()) return {};
	if (lists.size() == 1) return lists.front();
	
	// Merge all lists using set_union (removes duplicates)
	std::vector<std::string> result = lists.front();
	
	for (size_t i = 1; i < lists.size(); ++i) {
		const auto& next = lists[i];
		std::vector<std::string> tmp;
		tmp.reserve(result.size() + next.size()); // Reserve max possible size
		std::set_union(result.begin(), result.end(), next.begin(), next.end(), std::back_inserter(tmp));
		result.swap(tmp);
	}
	return result;
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeOrKeys(const DisjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeOrKeys");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.disjuncts", static_cast<int64_t>(q.disjuncts.size()));
	if (q.table.empty()) return {Status::Error("executeOrKeys: table darf nicht leer sein"), {}};
	if (q.disjuncts.empty()) return {Status::Error("executeOrKeys: keine Disjunkte"), {}};

	// Execute each disjunct (AND-block) and collect results
	std::vector<std::vector<std::string>> all_lists(q.disjuncts.size());
	std::vector<std::string> errors;
	tbb::task_group tg;

	for (size_t i = 0; i < q.disjuncts.size(); ++i) {
		const auto& disjunct = q.disjuncts[i];
		tg.run([this, &disjunct, &all_lists, i, &errors]() {
			auto child = Tracer::startSpan("or.disjunct.execute");
			child.setAttribute("disjunct.eq_count", static_cast<int64_t>(disjunct.predicates.size()));
			child.setAttribute("disjunct.range_count", static_cast<int64_t>(disjunct.rangePredicates.size()));
			auto [st, keys] = executeAndKeys(disjunct);
			if (!st.ok) {
				THEMIS_ERROR("Parallel OR disjunct error: {}", st.message);
				errors.push_back(st.message);
				child.setStatus(false, st.message);
				return;
			}
			// Sort for later union
			std::sort(keys.begin(), keys.end());
			all_lists[i] = std::move(keys);
			child.setAttribute("disjunct.result_count", static_cast<int64_t>(all_lists[i].size()));
			child.setStatus(true);
		});
	}
	tg.wait();

	if (!errors.empty()) {
		return {Status::Error("executeOrKeys: " + errors.front()), {}};
	}

	// Union all result sets
	auto keys = unionSortedLists_(std::move(all_lists));
	span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(keys)};
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeOrKeysWithFallback(const DisjunctiveQuery& q, bool optimize) const {
	auto span = Tracer::startSpan("QueryEngine.executeOrKeysWithFallback");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.disjuncts", static_cast<int64_t>(q.disjuncts.size()));
	if (q.table.empty()) return {Status::Error("executeOrKeysWithFallback: table darf nicht leer sein"), {}};
	if (q.disjuncts.empty()) return {Status::Error("executeOrKeysWithFallback: keine Disjunkte"), {}};

	std::vector<std::vector<std::string>> all_lists(q.disjuncts.size());
	tbb::task_group tg;
	for (size_t i = 0; i < q.disjuncts.size(); ++i) {
		const auto& disjunct = q.disjuncts[i];
		tg.run([this, &disjunct, &all_lists, i, optimize]() {
			auto child = Tracer::startSpan("or.disjunct.execute_fallback");
			child.setAttribute("disjunct.eq_count", static_cast<int64_t>(disjunct.predicates.size()));
			child.setAttribute("disjunct.range_count", static_cast<int64_t>(disjunct.rangePredicates.size()));
			auto [st, keys] = executeAndKeysWithFallback(disjunct, optimize);
			if (!st.ok) {
				THEMIS_ERROR("Parallel OR (fallback) disjunct error: {}", st.message);
				child.setStatus(false, st.message);
				return; // Dieser Disjunkt liefert keine Ergebnisse
			}
			std::sort(keys.begin(), keys.end());
			all_lists[i] = std::move(keys);
			child.setAttribute("disjunct.result_count", static_cast<int64_t>(all_lists[i].size()));
			child.setStatus(true);
		});
	}
	tg.wait();

	auto keys = unionSortedLists_(std::move(all_lists));
	span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(keys)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeOrEntitiesWithFallback(const DisjunctiveQuery& q, bool optimize) const {
	auto span = Tracer::startSpan("QueryEngine.executeOrEntitiesWithFallback");
	span.setAttribute("query.table", q.table);
	auto [st, keys] = executeOrKeysWithFallback(q, optimize);
	if (!st.ok) return {st, {}};

	// Parallel entity loading (analog zu executeOrEntities)
	constexpr size_t PARALLEL_THRESHOLD = 100;
	constexpr size_t BATCH_SIZE = 50;

	std::vector<BaseEntity> out;
	out.reserve(keys.size());

	if (keys.size() < PARALLEL_THRESHOLD) {
		for (const auto& pk : keys) {
			auto blob = db_.get(q.table + ":" + pk);
			if (!blob) continue;
			try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
			catch (...) { THEMIS_WARN("executeOrEntitiesWithFallback: Deserialisierung fehlgeschlagen für PK={}", pk); }
		}
	} else {
		std::vector<std::vector<BaseEntity>> batches((keys.size() + BATCH_SIZE - 1) / BATCH_SIZE);
		tbb::task_group tg;
		for (size_t batch_idx = 0; batch_idx < batches.size(); ++batch_idx) {
			tg.run([this, &q, &keys, &batches, batch_idx, BATCH_SIZE]() {
				size_t start = batch_idx * BATCH_SIZE;
				size_t end = std::min(start + BATCH_SIZE, keys.size());
				std::vector<BaseEntity> local_entities;
				local_entities.reserve(end - start);
				for (size_t i = start; i < end; ++i) {
					const auto& pk = keys[i];
					auto blob = db_.get(q.table + ":" + pk);
					if (!blob) continue;
					try { local_entities.emplace_back(BaseEntity::deserialize(pk, *blob)); }
					catch (...) { THEMIS_WARN("executeOrEntitiesWithFallback: Deserialisierung fehlgeschlagen für PK={}", pk); }
				}
				batches[batch_idx] = std::move(local_entities);
			});
		}
		tg.wait();
		for (auto& batch : batches) {
			out.insert(out.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
		}
	}

	span.setAttribute("query.entities_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(out)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeOrEntities(const DisjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeOrEntities");
	span.setAttribute("query.table", q.table);
	auto [st, keys] = executeOrKeys(q);
	if (!st.ok) return {st, {}};

	// Parallel entity loading (same logic as executeAndEntities)
	constexpr size_t PARALLEL_THRESHOLD = 100;
	constexpr size_t BATCH_SIZE = 50;

	std::vector<BaseEntity> out;
	out.reserve(keys.size());

	if (keys.size() < PARALLEL_THRESHOLD) {
		for (const auto& pk : keys) {
			auto blob = db_.get(q.table + ":" + pk);
			if (!blob) continue;
			try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
			catch (...) { THEMIS_WARN("executeOrEntities: Deserialisierung fehlgeschlagen für PK={}", pk); }
		}
	} else {
		std::vector<std::vector<BaseEntity>> batches((keys.size() + BATCH_SIZE - 1) / BATCH_SIZE);
		tbb::task_group tg;

		for (size_t batch_idx = 0; batch_idx < batches.size(); ++batch_idx) {
			tg.run([this, &q, &keys, &batches, batch_idx, BATCH_SIZE]() {
				size_t start = batch_idx * BATCH_SIZE;
				size_t end = std::min(start + BATCH_SIZE, keys.size());
				std::vector<BaseEntity> local_entities;
				local_entities.reserve(end - start);

				for (size_t i = start; i < end; ++i) {
					const auto& pk = keys[i];
					auto blob = db_.get(q.table + ":" + pk);
					if (!blob) continue;
					try { local_entities.emplace_back(BaseEntity::deserialize(pk, *blob)); }
					catch (...) { THEMIS_WARN("executeOrEntities: Deserialisierung fehlgeschlagen für PK={}", pk); }
				}

				batches[batch_idx] = std::move(local_entities);
			});
		}
		tg.wait();

		for (auto& batch : batches) {
			out.insert(out.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
		}
	}

	span.setAttribute("query.entities_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(out)};
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeysSequential(const std::string& table,
									  const std::vector<PredicateEq>& orderedPredicates) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndKeysSequential");
	span.setAttribute("query.table", table);
	span.setAttribute("query.eq_count", static_cast<int64_t>(orderedPredicates.size()));
	if (table.empty()) return {Status::Error("executeAndKeysSequential: table leer"), {}};
	if (orderedPredicates.empty()) return {Status::Error("executeAndKeysSequential: keine Prädikate"), {}};

	// Starte mit erster Liste
	{
		auto child = Tracer::startSpan("index.scanEqual");
		child.setAttribute("index.table", table);
		child.setAttribute("index.column", orderedPredicates[0].column);
		auto [st0, baseTmp] = secIdx_.scanKeysEqual(table, orderedPredicates[0].column, orderedPredicates[0].value);
		if (!st0.ok) { child.setStatus(false, st0.message); return {Status::Error("sequential: " + st0.message), {}}; }
		std::vector<std::string> base = std::move(baseTmp);
		std::sort(base.begin(), base.end());
		child.setAttribute("index.result_count", static_cast<int64_t>(base.size()));
		child.setStatus(true);
		if (base.empty()) { span.setStatus(true); return {Status::OK(), {}}; }
        
		std::vector<std::string> current = std::move(base);
		for (size_t i = 1; i < orderedPredicates.size(); ++i) {
			const auto& p = orderedPredicates[i];
			auto child2 = Tracer::startSpan("index.scanEqual");
			child2.setAttribute("index.table", table);
			child2.setAttribute("index.column", p.column);
			auto [st, keys] = secIdx_.scanKeysEqual(table, p.column, p.value);
			if (!st.ok) { child2.setStatus(false, st.message); return {Status::Error("sequential: " + st.message), {}}; }
			if (keys.empty()) { child2.setStatus(true); span.setStatus(true); return {Status::OK(), {}}; }
			std::sort(keys.begin(), keys.end());
			std::vector<std::string> tmp;
			tmp.reserve(std::min(current.size(), keys.size()));
			std::set_intersection(current.begin(), current.end(), keys.begin(), keys.end(), std::back_inserter(tmp));
			current.swap(tmp);
			child2.setAttribute("index.result_count", static_cast<int64_t>(current.size()));
			child2.setStatus(true);
			if (current.empty()) break;
		}
		span.setAttribute("query.result_count", static_cast<int64_t>(current.size()));
		span.setStatus(true);
		return {Status::OK(), std::move(current)};
	}
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntitiesSequential(const std::string& table,
										  const std::vector<PredicateEq>& orderedPredicates) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndEntitiesSequential");
	span.setAttribute("query.table", table);
	auto [st, keys] = executeAndKeysSequential(table, orderedPredicates);
	if (!st.ok) return {st, {}};

	// Paralleles Entity-Loading auch für Sequential-Variant
	constexpr size_t PARALLEL_THRESHOLD = 100;
	constexpr size_t BATCH_SIZE = 50;

	std::vector<BaseEntity> out;
	out.reserve(keys.size());

	if (keys.size() < PARALLEL_THRESHOLD) {
		// Sequential für kleine Mengen
		for (const auto& pk : keys) {
			auto blob = db_.get(table + ":" + pk);
			if (!blob) continue;
			try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
			catch (...) { THEMIS_WARN("executeAndEntitiesSequential: Deserialisierung fehlgeschlagen für PK={}", pk); }
		}
	} else {
		// Parallel für große Mengen
		std::vector<std::vector<BaseEntity>> batches((keys.size() + BATCH_SIZE - 1) / BATCH_SIZE);
		tbb::task_group tg;

		for (size_t batch_idx = 0; batch_idx < batches.size(); ++batch_idx) {
			tg.run([this, &table, &keys, &batches, batch_idx, BATCH_SIZE]() {
				size_t start = batch_idx * BATCH_SIZE;
				size_t end = std::min(start + BATCH_SIZE, keys.size());
				std::vector<BaseEntity> local_entities;
				local_entities.reserve(end - start);

				for (size_t i = start; i < end; ++i) {
					const auto& pk = keys[i];
					auto blob = db_.get(table + ":" + pk);
					if (!blob) continue;
					try { local_entities.emplace_back(BaseEntity::deserialize(pk, *blob)); }
					catch (...) { /* Silent failure in parallel context */ }
				}

				batches[batch_idx] = std::move(local_entities);
			});
		}
		tg.wait();

		// Merge batches
		for (auto& batch : batches) {
			out.insert(out.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
		}
	}

	span.setAttribute("query.entities_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(out)};
}

} // namespace themis

// ===================== QueryEngine Expression Evaluation =====================
namespace themis {

// Helper: convert json to boolean
static bool qe_toBool(const nlohmann::json& value) {
	if (value.is_boolean()) return value.get<bool>();
	if (value.is_number()) return value.get<double>() != 0.0;
	if (value.is_string()) return !value.get<std::string>().empty();
	if (value.is_null()) return false;
	if (value.is_array() || value.is_object()) return !value.empty();
	return false;
}

// Helper: convert json to number
static double qe_toNumber(const nlohmann::json& value) {
	if (value.is_number()) return value.get<double>();
	if (value.is_boolean()) return value.get<bool>() ? 1.0 : 0.0;
	if (value.is_string()) {
		try { return std::stod(value.get<std::string>()); } catch (...) { return 0.0; }
	}
	return 0.0;
}

// Helper: get nested field from json
static nlohmann::json qe_getNested(const nlohmann::json& obj, const std::vector<std::string>& path) {
	nlohmann::json current = obj;
	for (const auto& key : path) {
		if (current.is_object() && current.contains(key)) {
			current = current[key];
		} else if (current.is_array()) {
			try {
				size_t idx = static_cast<size_t>(std::stoull(key));
				if (idx < current.size()) current = current[idx]; else return nullptr;
			} catch (...) { return nullptr; }
		} else {
			return nullptr;
		}
	}
	return current;
}

// Forward decl
static nlohmann::json qe_evalExpr(const std::shared_ptr<themis::query::Expression>& expr,
								  const themis::QueryEngine::EvaluationContext& ctx);

static nlohmann::json qe_evalFunction(const std::string& funcName,
									  const std::vector<std::shared_ptr<themis::query::Expression>>& args,
									  const themis::QueryEngine::EvaluationContext& ctx) {
	using namespace themis::query;
	auto evalArg = [&](size_t i){ return qe_evalExpr(args[i], ctx); };

	// Basic string/number functions (subset, mirroring LetEvaluator)
	if (funcName == "LENGTH") {
		if (args.size() != 1) throw std::runtime_error("LENGTH expects 1 argument");
		auto v = evalArg(0);
		if (v.is_string()) return v.get<std::string>().length();
		if (v.is_array() || v.is_object()) return v.size();
		return 0;
	}
	if (funcName == "CONCAT") {
		std::string out;
		for (size_t i = 0; i < args.size(); ++i) {
			auto v = evalArg(i);
			out += v.is_string() ? v.get<std::string>() : v.dump();
		}
		return out;
	}
	if (funcName == "SUBSTRING") {
		if (args.size() < 2 || args.size() > 3) throw std::runtime_error("SUBSTRING expects 2 or 3 arguments");
		auto s = evalArg(0);
		auto st = evalArg(1);
		if (!s.is_string()) throw std::runtime_error("SUBSTRING expects string as first argument");
		std::string sv = s.get<std::string>();
		size_t startIdx = static_cast<size_t>(qe_toNumber(st));
		if (startIdx >= sv.size()) return "";
		if (args.size() == 3) {
			size_t len = static_cast<size_t>(qe_toNumber(evalArg(2)));
			return sv.substr(startIdx, len);
		}
		return sv.substr(startIdx);
	}
	if (funcName == "UPPER" || funcName == "LOWER") {
		if (args.size() != 1) throw std::runtime_error("UPPER/LOWER expects 1 argument");
		auto v = evalArg(0);
		if (!v.is_string()) throw std::runtime_error("UPPER/LOWER expects string argument");
		std::string s = v.get<std::string>();
		if (funcName == "UPPER") std::transform(s.begin(), s.end(), s.begin(), ::toupper);
		else std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		return s;
	}
	if (funcName == "ABS" || funcName == "CEIL" || funcName == "FLOOR" || funcName == "ROUND") {
		if (args.size() != 1) throw std::runtime_error("ABS/CEIL/FLOOR/ROUND expects 1 argument");
		double x = qe_toNumber(evalArg(0));
		if (funcName == "ABS") return std::abs(x);
		if (funcName == "CEIL") return std::ceil(x);
		if (funcName == "FLOOR") return std::floor(x);
		return std::round(x);
	}
	if (funcName == "MIN" || funcName == "MAX") {
		if (args.empty()) throw std::runtime_error("MIN/MAX expects at least 1 argument");
		double val = qe_toNumber(evalArg(0));
		for (size_t i = 1; i < args.size(); ++i) {
			double x = qe_toNumber(evalArg(i));
			if (funcName == "MIN") val = std::min(val, x); else val = std::max(val, x);
		}
		return val;
	}

	// ================= SPATIAL (ST_*) =================
	if (funcName == "ST_Point") {
		if (args.size() != 2) throw std::runtime_error("ST_Point expects 2 arguments");
		double x = qe_toNumber(evalArg(0));
		double y = qe_toNumber(evalArg(1));
		nlohmann::json g; g["type"] = "Point"; g["coordinates"] = {x, y}; return g;
	}

	if (funcName == "ST_AsGeoJSON") {
		if (args.size() != 1) throw std::runtime_error("ST_AsGeoJSON expects 1 argument");
		auto geom = evalArg(0);
		if (geom.is_object() && geom.contains("type") && geom.contains("coordinates")) return geom.dump();
		if (geom.is_string()) {
			// Treat string as EWKB bytes (simplified like LetEvaluator)
			std::string ewkbStr = geom.get<std::string>();
			std::vector<uint8_t> ewkb(ewkbStr.begin(), ewkbStr.end());
			try {
				auto geomInfo = utils::geo::parseEWKB(ewkb);
				nlohmann::json geojson;
				switch (geomInfo.type) {
					case utils::geo::GeometryType::Point:
					case utils::geo::GeometryType::PointZ: {
						geojson["type"] = "Point";
						if (!geomInfo.coordinates.empty()) {
							const auto& c = geomInfo.coordinates[0];
							geojson["coordinates"] = {c.x, c.y};
							if (geomInfo.type == utils::geo::GeometryType::PointZ) geojson["coordinates"].push_back(c.z);
						}
						break; }
					case utils::geo::GeometryType::LineString:
					case utils::geo::GeometryType::LineStringZ: {
						geojson["type"] = "LineString";
						geojson["coordinates"] = nlohmann::json::array();
						for (const auto& c : geomInfo.coordinates) {
							if (geomInfo.type == utils::geo::GeometryType::LineStringZ) geojson["coordinates"].push_back({c.x, c.y, c.z});
							else geojson["coordinates"].push_back({c.x, c.y});
						}
						break; }
					case utils::geo::GeometryType::Polygon:
					case utils::geo::GeometryType::PolygonZ: {
						geojson["type"] = "Polygon";
						nlohmann::json ring = nlohmann::json::array();
						for (const auto& c : geomInfo.coordinates) {
							if (geomInfo.type == utils::geo::GeometryType::PolygonZ) ring.push_back({c.x, c.y, c.z});
							else ring.push_back({c.x, c.y});
						}
						geojson["coordinates"] = nlohmann::json::array({ring});
						break; }
					default: throw std::runtime_error("ST_AsGeoJSON: Unsupported geometry type");
				}
				return geojson.dump();
			} catch (const std::exception& e) {
				throw std::runtime_error(std::string("ST_AsGeoJSON: Failed to parse EWKB: ") + e.what());
			}
		}
		throw std::runtime_error("ST_AsGeoJSON: Argument must be GeoJSON object or EWKB binary");
	}

	if (funcName == "ST_Distance") {
		if (args.size() != 2) throw std::runtime_error("ST_Distance expects 2 arguments");
		auto g1 = evalArg(0); auto g2 = evalArg(1);
		auto extractPoint = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type") && g["type"]=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2) {
				return std::pair<double,double>(g["coordinates"][0].get<double>(), g["coordinates"][1].get<double>());
			}
			throw std::runtime_error("ST_Distance: Expected Point geometry");
		};
		auto [x1,y1] = extractPoint(g1); auto [x2,y2] = extractPoint(g2);
		double dx=x2-x1, dy=y2-y1; return std::sqrt(dx*dx+dy*dy);
	}

	if (funcName == "ST_Intersects") {
		if (args.size() != 2) throw std::runtime_error("ST_Intersects expects 2 arguments");
		auto g1 = evalArg(0); auto g2 = evalArg(1);
		auto extractPoint = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type") && g["type"]=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2)
				return std::pair<double,double>(g["coordinates"][0].get<double>(), g["coordinates"][1].get<double>());
			throw std::runtime_error("ST_Intersects: Expected Point geometry");
		};
		auto [x1,y1] = extractPoint(g1); auto [x2,y2] = extractPoint(g2);
		const double eps=1e-9; return (std::abs(x1-x2)<eps && std::abs(y1-y2)<eps);
	}

	if (funcName == "ST_Within") {
		if (args.size() != 2) throw std::runtime_error("ST_Within expects 2 arguments");
		auto p = evalArg(0); auto poly = evalArg(1);
		auto extractPoint = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type") && g["type"]=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2)
				return std::pair<double,double>(g["coordinates"][0].get<double>(), g["coordinates"][1].get<double>());
			throw std::runtime_error("ST_Within: Expected Point geometry");
		};
		auto extractMBR = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type")) {
				std::string t = g["type"];
				if (t=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2) {
					double x=g["coordinates"][0].get<double>(), y=g["coordinates"][1].get<double>();
					return utils::geo::MBR{x,y,x,y};
				}
				if (t=="Polygon" && g.contains("coordinates")) {
					const auto& rings = g["coordinates"];
					if (rings.is_array() && !rings.empty()) {
						const auto& ext = rings[0];
						double minx=std::numeric_limits<double>::max(), miny=std::numeric_limits<double>::max();
						double maxx=std::numeric_limits<double>::lowest(), maxy=std::numeric_limits<double>::lowest();
						for (const auto& c : ext) if (c.is_array() && c.size()>=2) {
							double x=c[0].get<double>(), y=c[1].get<double>();
							minx=std::min(minx,x); miny=std::min(miny,y); maxx=std::max(maxx,x); maxy=std::max(maxy,y);
						}
						return utils::geo::MBR{minx,miny,maxx,maxy};
					}
				}
			}
			throw std::runtime_error("ST_Within: Could not extract MBR");
		};
		auto [px,py]=extractPoint(p); auto m=extractMBR(poly);
		return (px>=m.minx && px<=m.maxx && py>=m.miny && py<=m.maxy);
	}

	if (funcName == "ST_Contains") {
		if (args.size() != 2) throw std::runtime_error("ST_Contains expects 2 arguments");
		auto g1 = evalArg(0); auto g2 = evalArg(1);
		auto extractMBR = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type")) {
				std::string t=g["type"];
				if (t=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2) {
					double x=g["coordinates"][0].get<double>(), y=g["coordinates"][1].get<double>();
					return utils::geo::MBR{x,y,x,y};
				}
				if (t=="Polygon" && g.contains("coordinates")) {
					const auto& rings=g["coordinates"];
					if (rings.is_array() && !rings.empty()) {
						const auto& ext=rings[0];
						double minx=std::numeric_limits<double>::max(), miny=std::numeric_limits<double>::max();
						double maxx=std::numeric_limits<double>::lowest(), maxy=std::numeric_limits<double>::lowest();
						for (const auto& c : ext) if (c.is_array() && c.size()>=2) {
							double x=c[0].get<double>(), y=c[1].get<double>();
							minx=std::min(minx,x); miny=std::min(miny,y); maxx=std::max(maxx,x); maxy=std::max(maxy,y);
						}
						return utils::geo::MBR{minx,miny,maxx,maxy};
					}
				}
			}
			throw std::runtime_error("ST_Contains: Could not extract MBR");
		};
		auto m1=extractMBR(g1); auto m2=extractMBR(g2);
		return (m2.minx>=m1.minx && m2.maxx<=m1.maxx && m2.miny>=m1.miny && m2.maxy<=m1.maxy);
	}

	if (funcName == "ST_DWithin") {
		if (args.size() != 3) throw std::runtime_error("ST_DWithin expects 3 arguments");
		auto g1=evalArg(0), g2=evalArg(1); double maxd = qe_toNumber(evalArg(2));
		auto extractPoint = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type") && g["type"]=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2)
				return std::pair<double,double>(g["coordinates"][0].get<double>(), g["coordinates"][1].get<double>());
			throw std::runtime_error("ST_DWithin: Expected Point geometry");
		};
		auto [x1,y1]=extractPoint(g1); auto [x2,y2]=extractPoint(g2);
		double dx=x2-x1, dy=y2-y1; double d=std::sqrt(dx*dx+dy*dy);
		return d <= maxd;
	}

	if (funcName == "ST_HasZ") {
		if (args.size() != 1) throw std::runtime_error("ST_HasZ expects 1 argument");
		auto g = evalArg(0);
		if (g.is_object() && g.contains("type") && g.contains("coordinates")) {
			const auto& c = g["coordinates"]; std::string t = g["type"];
			if (t=="Point" && c.is_array() && c.size()>=3) return true;
			if ((t=="LineString"||t=="MultiPoint") && c.is_array() && !c.empty() && c[0].is_array() && c[0].size()>=3) return true;
			if (t=="Polygon" && c.is_array() && !c.empty() && c[0].is_array() && !c[0].empty() && c[0][0].is_array() && c[0][0].size()>=3) return true;
		}
		return false;
	}

	if (funcName == "ST_Z") {
		if (args.size() != 1) throw std::runtime_error("ST_Z expects 1 argument");
		auto g = evalArg(0);
		if (g.is_object() && g.contains("type") && g["type"]=="Point" && g.contains("coordinates") && g["coordinates"].is_array() && g["coordinates"].size()>=3) return g["coordinates"][2];
		return nlohmann::json(nullptr);
	}

	if (funcName == "ST_ZMin" || funcName == "ST_ZMax") {
		if (args.size() != 1) throw std::runtime_error("ST_ZMin/ZMax expects 1 argument");
		auto g = evalArg(0);
		if (!g.is_object() || !g.contains("type") || !g.contains("coordinates")) return nlohmann::json(nullptr);
		std::string t = g["type"]; const auto& coords = g["coordinates"];
		double acc = (funcName=="ST_ZMin") ? std::numeric_limits<double>::max() : std::numeric_limits<double>::lowest();
		bool hasZ=false;
		auto upd = [&](double z){ if (funcName=="ST_ZMin") acc = std::min(acc, z); else acc = std::max(acc, z); hasZ=true; };
		if (t=="Point" && coords.is_array() && coords.size()>=3) return coords[2];
		if ((t=="LineString"||t=="MultiPoint") && coords.is_array()) {
			for (const auto& pt : coords) if (pt.is_array() && pt.size()>=3) upd(pt[2].get<double>());
		} else if (t=="Polygon" && coords.is_array()) {
			for (const auto& ring : coords) if (ring.is_array()) for (const auto& pt : ring) if (pt.is_array() && pt.size()>=3) upd(pt[2].get<double>());
		}
		return hasZ ? nlohmann::json(acc) : nlohmann::json(nullptr);
	}

	if (funcName == "ST_GeomFromText") {
		if (args.size() != 1) throw std::runtime_error("ST_GeomFromText expects 1 argument");
		auto w = evalArg(0);
		if (!w.is_string()) throw std::runtime_error("ST_GeomFromText: Argument must be WKT string");
		std::string wkt = w.get<std::string>();
		auto trim = [](std::string s){ s.erase(0, s.find_first_not_of(" \t\n\r")); s.erase(s.find_last_not_of(" \t\n\r")+1); return s; };
		std::string u = trim(wkt); std::string up=u; std::transform(up.begin(), up.end(), up.begin(), ::toupper);
		nlohmann::json geojson;
		if (up.rfind("POINT",0)==0) {
			size_t a=up.find('('), b=up.find(')'); if (a==std::string::npos||b==std::string::npos) throw std::runtime_error("Invalid POINT WKT");
			std::string coords = u.substr(a+1, b-a-1);
			std::istringstream iss(coords); double x,y,z;
			if (!(iss>>x>>y)) throw std::runtime_error("Invalid POINT coords");
			geojson["type"]="Point"; if (iss>>z) geojson["coordinates"]={x,y,z}; else geojson["coordinates"]={x,y};
			return geojson;
		}
		if (up.rfind("LINESTRING",0)==0) {
			size_t a=up.find('('), b=up.find(')'); if (a==std::string::npos||b==std::string::npos) throw std::runtime_error("Invalid LINESTRING WKT");
			std::string coordsStr = u.substr(a+1, b-a-1);
			std::replace(coordsStr.begin(), coordsStr.end(), ',', ' ');
			std::istringstream iss(coordsStr); nlohmann::json arr = nlohmann::json::array(); double x,y,z;
			while (iss>>x>>y) { if (iss>>z) arr.push_back({x,y,z}); else arr.push_back({x,y}); }
			geojson["type"]="LineString"; geojson["coordinates"]=arr; return geojson;
		}
		if (up.rfind("POLYGON",0)==0) {
			size_t a=up.find("(("), b=up.find("))"); if (a==std::string::npos||b==std::string::npos || b<=a+1) throw std::runtime_error("Invalid POLYGON WKT");
			std::string inner = u.substr(a+2, b-(a+2));
			std::istringstream iss(inner); nlohmann::json ring = nlohmann::json::array(); double x,y,z; char comma;
			while (iss>>x>>y) {
				if (iss.peek()==' ') { iss.get(); }
				if (std::isdigit(iss.peek())||iss.peek()=='-'||iss.peek()=='+') { if (iss>>z) ring.push_back({x,y,z}); else ring.push_back({x,y}); }
				else ring.push_back({x,y});
				if (iss.peek()==',') iss.get();
			}
			nlohmann::json coords = nlohmann::json::array(); coords.push_back(ring);
			nlohmann::json poly; poly["type"]="Polygon"; poly["coordinates"]=coords; return poly;
		}
		throw std::runtime_error("ST_GeomFromText: Unsupported WKT (POINT, LINESTRING, POLYGON)");
	}

	if (funcName == "ST_AsText") {
		if (args.size() != 1) throw std::runtime_error("ST_AsText expects 1 argument");
		auto g = evalArg(0);
		if (!g.is_object() || !g.contains("type") || !g.contains("coordinates")) throw std::runtime_error("ST_AsText: Invalid geometry object");
		std::string t = g["type"]; const auto& c = g["coordinates"]; std::ostringstream wkt;
		if (t=="Point") {
			if (!c.is_array() || c.size()<2) throw std::runtime_error("ST_AsText: Invalid Point");
			wkt<<"POINT("<<c[0].get<double>()<<" "<<c[1].get<double>(); if (c.size()>=3) wkt<<" "<<c[2].get<double>(); wkt<<")"; return wkt.str();
		} else if (t=="LineString") {
			if (!c.is_array()||c.empty()) throw std::runtime_error("ST_AsText: Invalid LineString");
			wkt<<"LINESTRING("; for (size_t i=0;i<c.size();++i){ if(i>0) wkt<<","; const auto& pt=c[i]; wkt<<pt[0].get<double>()<<" "<<pt[1].get<double>(); if (pt.size()>=3) wkt<<" "<<pt[2].get<double>(); } wkt<<")"; return wkt.str();
		} else if (t=="Polygon") {
			if (!c.is_array()||c.empty()) throw std::runtime_error("ST_AsText: Invalid Polygon");
			wkt<<"POLYGON("; for (size_t r=0;r<c.size();++r){ if(r>0) wkt<<","; wkt<<"("; const auto& ring=c[r]; for(size_t i=0;i<ring.size();++i){ if(i>0) wkt<<","; const auto& pt=ring[i]; wkt<<pt[0].get<double>()<<" "<<pt[1].get<double>(); if (pt.size()>=3) wkt<<" "<<pt[2].get<double>(); } wkt<<")"; } wkt<<")"; return wkt.str();
		}
		throw std::runtime_error("ST_AsText: Unsupported geometry type");
	}

	if (funcName == "ST_3DDistance") {
		if (args.size() != 2) throw std::runtime_error("ST_3DDistance expects 2 arguments");
		auto g1=evalArg(0), g2=evalArg(1);
		auto extract = [](const nlohmann::json& g){
			if (g.is_object() && g.contains("type") && g["type"]=="Point" && g.contains("coordinates") && g["coordinates"].is_array()) {
				const auto& a=g["coordinates"]; if (a.size()>=2) { double x=a[0].get<double>(), y=a[1].get<double>(); double z = a.size()>=3 ? a[2].get<double>() : 0.0; return std::tuple<double,double,double>(x,y,z); }
			}
			throw std::runtime_error("ST_3DDistance: Expected Point");
		};
		auto [x1,y1,z1]=extract(g1); auto [x2,y2,z2]=extract(g2); double dx=x2-x1, dy=y2-y1, dz=z2-z1; return std::sqrt(dx*dx+dy*dy+dz*dz);
	}

	if (funcName == "ST_Force2D") {
		if (args.size() != 1) throw std::runtime_error("ST_Force2D expects 1 argument");
		auto g = evalArg(0);
		if (!g.is_object() || !g.contains("type") || !g.contains("coordinates")) return g;
		nlohmann::json result = g; std::string t=g["type"];
		auto strip2D = [](const nlohmann::json& coord){ if (coord.is_array() && coord.size()>=2) return nlohmann::json::array({coord[0], coord[1]}); return coord; };
		if (t=="Point") result["coordinates"]=strip2D(g["coordinates"]);
		else if (t=="LineString"||t=="MultiPoint") { nlohmann::json nc=nlohmann::json::array(); for (const auto& pt : g["coordinates"]) nc.push_back(strip2D(pt)); result["coordinates"]=nc; }
		else if (t=="Polygon"||t=="MultiLineString") { nlohmann::json nr=nlohmann::json::array(); for (const auto& ring : g["coordinates"]) { nlohmann::json r=nlohmann::json::array(); for (const auto& pt : ring) r.push_back(strip2D(pt)); nr.push_back(r);} result["coordinates"]=nr; }
		return result;
	}

	if (funcName == "ST_ZBetween") {
		if (args.size() != 3) throw std::runtime_error("ST_ZBetween expects 3 arguments");
		auto g = evalArg(0); double zmin = qe_toNumber(evalArg(1)); double zmax = qe_toNumber(evalArg(2));
		if (!g.is_object() || !g.contains("type") || !g.contains("coordinates")) return false;
		std::string t=g["type"]; const auto& c=g["coordinates"]; auto inRange=[&](double z){ return z>=zmin && z<=zmax; };
		if (t=="Point") { if (c.is_array() && c.size()>=3) return inRange(c[2].get<double>()); return false; }
		if (t=="LineString"||t=="MultiPoint") { if (c.is_array()) { for (const auto& pt : c) if (pt.is_array() && pt.size()>=3 && inRange(pt[2].get<double>())) return true; } return false; }
		if (t=="Polygon"||t=="MultiLineString") { if (c.is_array()) { for (const auto& ring : c) if (ring.is_array()) for (const auto& pt : ring) if (pt.is_array() && pt.size()>=3 && inRange(pt[2].get<double>())) return true; } return false; }
		return false;
	}

	if (funcName == "ST_Buffer") {
		if (args.size() != 2) throw std::runtime_error("ST_Buffer expects 2 arguments");
		auto g = evalArg(0); double dist = qe_toNumber(evalArg(1));
		if (!g.is_object() || !g.contains("type") || !g.contains("coordinates")) throw std::runtime_error("ST_Buffer: invalid geometry");
		std::string t=g["type"];
		if (t=="Point") {
			const auto& c=g["coordinates"]; if (!c.is_array()||c.size()<2) throw std::runtime_error("ST_Buffer: invalid Point");
			double x=c[0].get<double>(), y=c[1].get<double>();
			nlohmann::json ring = nlohmann::json::array({ {x-dist,y-dist},{x+dist,y-dist},{x+dist,y+dist},{x-dist,y+dist},{x-dist,y-dist} });
			nlohmann::json poly; poly["type"]="Polygon"; poly["coordinates"]=nlohmann::json::array({ring}); return poly;
		}
		if (t=="Polygon") {
			const auto& rings=g["coordinates"]; if (!rings.is_array()||rings.empty()) throw std::runtime_error("ST_Buffer: invalid Polygon");
			const auto& ext=rings[0]; double minx=std::numeric_limits<double>::max(), miny=std::numeric_limits<double>::max(); double maxx=std::numeric_limits<double>::lowest(), maxy=std::numeric_limits<double>::lowest();
			for (const auto& pt : ext) if (pt.is_array()&&pt.size()>=2){ double x=pt[0].get<double>(), y=pt[1].get<double>(); minx=std::min(minx,x); miny=std::min(miny,y); maxx=std::max(maxx,x); maxy=std::max(maxy,y);} 
			minx-=dist; miny-=dist; maxx+=dist; maxy+=dist;
			nlohmann::json ring=nlohmann::json::array({ {minx,miny},{maxx,miny},{maxx,maxy},{minx,maxy},{minx,miny} });
			nlohmann::json poly; poly["type"]="Polygon"; poly["coordinates"]=nlohmann::json::array({ring}); return poly;
		}
		return g;
	}

	if (funcName == "ST_Union") {
		if (args.size() != 2) throw std::runtime_error("ST_Union expects 2 arguments");
		auto g1=evalArg(0), g2=evalArg(1);
		auto mbrOf=[](const nlohmann::json& g){
			if (g.is_object() && g.contains("type")){
				std::string t=g["type"];
				if (t=="Point" && g.contains("coordinates") && g["coordinates"].size()>=2){ double x=g["coordinates"][0].get<double>(), y=g["coordinates"][1].get<double>(); return utils::geo::MBR{x,y,x,y}; }
				if (t=="Polygon" && g.contains("coordinates")){
					const auto& rings=g["coordinates"]; if (rings.is_array()&&!rings.empty()){
						double minx=std::numeric_limits<double>::max(),miny=std::numeric_limits<double>::max(); double maxx=std::numeric_limits<double>::lowest(),maxy=std::numeric_limits<double>::lowest();
						const auto& ext=rings[0]; for (const auto& pt:ext) if (pt.is_array()&&pt.size()>=2){ double x=pt[0].get<double>(), y=pt[1].get<double>(); minx=std::min(minx,x); miny=std::min(miny,y); maxx=std::max(maxx,x); maxy=std::max(maxy,y);} 
						return utils::geo::MBR{minx,miny,maxx,maxy};
					}
				}
			}
			throw std::runtime_error("ST_Union: Unsupported geometry type for MVP");
		};
		auto m1=mbrOf(g1), m2=mbrOf(g2);
		utils::geo::MBR u{ std::min(m1.minx,m2.minx), std::min(m1.miny,m2.miny), std::max(m1.maxx,m2.maxx), std::max(m1.maxy,m2.maxy) };
		nlohmann::json ring=nlohmann::json::array({ {u.minx,u.miny},{u.maxx,u.miny},{u.maxx,u.maxy},{u.minx,u.maxy},{u.minx,u.miny} });
		nlohmann::json poly; poly["type"]="Polygon"; poly["coordinates"]=nlohmann::json::array({ring}); return poly;
	}

	throw std::runtime_error("Unknown function: " + funcName);
}

static nlohmann::json qe_evalExpr(const std::shared_ptr<themis::query::Expression>& expr,
								  const themis::QueryEngine::EvaluationContext& ctx) {
	using namespace themis::query;
	if (!expr) return nlohmann::json(nullptr);

	switch (expr->getType()) {
		case ASTNodeType::Literal: {
			auto lit = std::static_pointer_cast<LiteralExpr>(expr);
			nlohmann::json j;
			std::visit([&](auto&& arg){ j = arg; }, lit->value);
			return j;
		}
		case ASTNodeType::Variable: {
			auto v = std::static_pointer_cast<VariableExpr>(expr);
			auto bound = ctx.get(v->name);
			return bound.has_value() ? bound.value() : nlohmann::json(nullptr);
		}
		case ASTNodeType::FieldAccess: {
			auto fa = std::static_pointer_cast<FieldAccessExpr>(expr);
			auto base = qe_evalExpr(fa->object, ctx);
			if (base.is_null()) return nullptr;
			return qe_getNested(base, {fa->field});
		}
		case ASTNodeType::ArrayLiteral: {
			auto arr = std::static_pointer_cast<ArrayLiteralExpr>(expr);
			nlohmann::json a = nlohmann::json::array();
			for (const auto& e : arr->elements) a.push_back(qe_evalExpr(e, ctx));
			return a;
		}
		case ASTNodeType::ObjectConstruct: {
			auto obj = std::static_pointer_cast<ObjectConstructExpr>(expr);
			nlohmann::json o = nlohmann::json::object();
			for (const auto& [k, e] : obj->fields) o[k] = qe_evalExpr(e, ctx);
			return o;
		}
		case ASTNodeType::UnaryOp: {
			auto u = std::static_pointer_cast<UnaryOpExpr>(expr);
			auto v = qe_evalExpr(u->operand, ctx);
			if (u->op == UnaryOperator::Not) return !qe_toBool(v);
			if (u->op == UnaryOperator::Minus) return -qe_toNumber(v);
			if (u->op == UnaryOperator::Plus) return qe_toNumber(v);
			throw std::runtime_error("Unknown unary operator");
		}
		case ASTNodeType::BinaryOp: {
			auto b = std::static_pointer_cast<BinaryOpExpr>(expr);
			auto l = qe_evalExpr(b->left, ctx);
			auto r = qe_evalExpr(b->right, ctx);
			switch (b->op) {
				case BinaryOperator::Add: return qe_toNumber(l) + qe_toNumber(r);
				case BinaryOperator::Sub: return qe_toNumber(l) - qe_toNumber(r);
				case BinaryOperator::Mul: return qe_toNumber(l) * qe_toNumber(r);
				case BinaryOperator::Div: {
					double d = qe_toNumber(r); if (d==0.0) throw std::runtime_error("Division by zero"); return qe_toNumber(l)/d; }
				case BinaryOperator::Mod: {
					double d = qe_toNumber(r); if (d==0.0) throw std::runtime_error("Modulo by zero"); return std::fmod(qe_toNumber(l), d); }
				case BinaryOperator::Eq: return l == r;
				case BinaryOperator::Neq: return l != r;
				case BinaryOperator::Lt: return l.is_number()&&r.is_number() ? (l.get<double>() < r.get<double>()) : (l.dump() < r.dump());
				case BinaryOperator::Lte: return l.is_number()&&r.is_number() ? (l.get<double>() <= r.get<double>()) : (l.dump() <= r.dump());
				case BinaryOperator::Gt: return l.is_number()&&r.is_number() ? (l.get<double>() > r.get<double>()) : (l.dump() > r.dump());
				case BinaryOperator::Gte: return l.is_number()&&r.is_number() ? (l.get<double>() >= r.get<double>()) : (l.dump() >= r.dump());
				case BinaryOperator::And: return qe_toBool(l) && qe_toBool(r);
				case BinaryOperator::Or: return qe_toBool(l) || qe_toBool(r);
				case BinaryOperator::Xor: return qe_toBool(l) ^ qe_toBool(r);
				case BinaryOperator::In: {
					// Membership: left IN right (right can be array or string)
					if (r.is_array()) {
						for (const auto& e : r) if (e == l) return true; return false;
					}
					if (r.is_string() && l.is_string()) {
						return r.get<std::string>().find(l.get<std::string>()) != std::string::npos;
					}
					return false;
				}
			}
			throw std::runtime_error("Unknown binary operator");
		}
		case ASTNodeType::FunctionCall: {
			auto f = std::static_pointer_cast<FunctionCallExpr>(expr);
			return qe_evalFunction(f->name, f->arguments, ctx);
		}
		default: break;
	}
	throw std::runtime_error("Unknown expression type in QueryEngine evaluator");
}

nlohmann::json QueryEngine::evaluateExpression(
	const std::shared_ptr<query::Expression>& expr,
	const EvaluationContext& ctx
) const {
	return qe_evalExpr(expr, ctx);
}

bool QueryEngine::evaluateCondition(
	const std::shared_ptr<query::Expression>& expr,
	const EvaluationContext& ctx
) const {
	return qe_toBool(qe_evalExpr(expr, ctx));
}

} // namespace themis

namespace themis {

std::vector<std::string> QueryEngine::fullScanAndFilter_(const ConjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.fullScan");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.eq_count", static_cast<int64_t>(q.predicates.size()));
	span.setAttribute("query.range_count", static_cast<int64_t>(q.rangePredicates.size()));
	std::vector<std::string> out;
	if (q.table.empty()) return out;
	const std::string prefix = q.table + ":";
	int64_t scanned = 0;
	
	// Helper for numeric comparison: try to parse as numbers, fall back to string comparison
	auto compareValues = [](const std::string& a, const std::string& b) -> int {
		try {
			// Try integer comparison first
			size_t pos_a = 0, pos_b = 0;
			long long num_a = std::stoll(a, &pos_a);
			long long num_b = std::stoll(b, &pos_b);
			
			// Only use numeric comparison if entire strings parsed
			if (pos_a == a.size() && pos_b == b.size()) {
				if (num_a < num_b) return -1;
				if (num_a > num_b) return 1;
				return 0;
			}
		} catch (...) {
			// Not integers, try doubles
			try {
				size_t pos_a = 0, pos_b = 0;
				double num_a = std::stod(a, &pos_a);
				double num_b = std::stod(b, &pos_b);
				
				if (pos_a == a.size() && pos_b == b.size()) {
					if (num_a < num_b) return -1;
					if (num_a > num_b) return 1;
					return 0;
				}
			} catch (...) {}
		}
		
		// Fall back to lexicographic string comparison
		return a.compare(b);
	};
	
	db_.scanPrefix(prefix, [&](std::string_view key, std::string_view value){
		// Deserialize entity and test all predicates
		std::string pk = KeySchema::extractPrimaryKey(key);
		std::vector<uint8_t> blob(value.begin(), value.end());
		try {
			BaseEntity e = BaseEntity::deserialize(pk, blob);
			++scanned;
			bool match = true;
			for (const auto& p : q.predicates) {
				auto v = e.extractField(p.column);
				if (!v || *v != p.value) { match = false; break; }
			}
			// Range-Prädikate mit numerischem Vergleich
			if (match) {
				for (const auto& r : q.rangePredicates) {
					auto v = e.extractField(r.column);
					if (!v) { match = false; break; }
					if (r.lower.has_value()) {
						int cmp = compareValues(*v, *r.lower);
						if (cmp < 0 || (cmp == 0 && !r.includeLower)) { match = false; break; }
					}
					if (r.upper.has_value()) {
						int cmp = compareValues(*v, *r.upper);
						if (cmp > 0 || (cmp == 0 && !r.includeUpper)) { match = false; break; }
					}
				}
			}
			if (match) out.push_back(std::move(pk));
		} catch (...) {
			// skip malformed entries
		}
		return true;
	});
	span.setAttribute("fullscan.scanned", scanned);
	span.setAttribute("query.result_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return out;
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeysWithFallback(const ConjunctiveQuery& q, bool optimize) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndKeysWithFallback");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.eq_count", static_cast<int64_t>(q.predicates.size()));
	span.setAttribute("query.range_count", static_cast<int64_t>(q.rangePredicates.size()));
	// If no predicates at all, must do full scan
	if (q.predicates.empty() && q.rangePredicates.empty() && !q.orderBy.has_value()) {
		auto keys = fullScanAndFilter_(q);
		span.setAttribute("query.exec_mode", "full_scan");
		span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
		span.setStatus(true);
		return {Status::OK(), std::move(keys)};
	}
	
	// Try index-based path; on failure due to missing index, fallback to full scan
	bool missingIndex = false;

	// Prüfe Gleichheitsindizes
	if (!q.predicates.empty()) {
		size_t bestIdx = 0; size_t bestEst = SIZE_MAX; bool bestCapped=false;
		for (size_t i=0;i<q.predicates.size();++i) {
			bool capped=false; size_t est = secIdx_.estimateCountEqual(q.table, q.predicates[i].column, q.predicates[i].value, 16, &capped);
			size_t eff = capped ? 16 : est;
			if (eff < bestEst) { bestEst = eff; bestIdx = i; bestCapped = capped; }
		}
		{
			auto [st, _] = secIdx_.scanKeysEqual(q.table, q.predicates[bestIdx].column, q.predicates[bestIdx].value);
			if (!st.ok) missingIndex = true;
		}
	}

	// Prüfe Range-Indizes
	for (const auto& r : q.rangePredicates) {
		if (!secIdx_.hasRangeIndex(q.table, r.column)) { missingIndex = true; break; }
	}

	if (!missingIndex) {
		if (!q.rangePredicates.empty() || q.orderBy.has_value()) {
			auto [st, keys] = executeAndKeysRangeAware_(q);
			if (!st.ok) { span.setStatus(false, st.message); return {st, {}}; }
			span.setAttribute("query.exec_mode", "range_aware");
			span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
			span.setStatus(true);
			return {Status::OK(), std::move(keys)};
		}
		if (optimize) {
			QueryOptimizer opt(secIdx_);
			auto plan = opt.chooseOrderForAndQuery(q);
			auto [st, keys] = executeAndKeysSequential(q.table, plan.orderedPredicates);
			if (!st.ok) { span.setStatus(false, st.message); return {st, {}}; }
			span.setAttribute("query.exec_mode", "index_optimized");
			span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
			span.setStatus(true);
			return {Status::OK(), std::move(keys)};
		}
		auto [st, keys] = executeAndKeys(q);
		if (!st.ok) { span.setStatus(false, st.message); return {st, {}}; }
		span.setAttribute("query.exec_mode", "index_parallel");
		span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
		span.setStatus(true);
		return {Status::OK(), std::move(keys)};
	}

	// Fallback: full scan (inkl. Range-Prädikate)
	auto keys = fullScanAndFilter_(q);
	span.setAttribute("query.exec_mode", "full_scan_fallback");
	span.setAttribute("query.result_count", static_cast<int64_t>(keys.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(keys)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntitiesWithFallback(const ConjunctiveQuery& q, bool optimize) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndEntitiesWithFallback");
	span.setAttribute("query.table", q.table);
	auto [st, keys] = executeAndKeysWithFallback(q, optimize);
	if (!st.ok) return {st, {}};
	std::vector<BaseEntity> out; out.reserve(keys.size());
	for (const auto& pk : keys) {
		auto blob = db_.get(q.table + ":" + pk);
		if (!blob) continue;
		try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
		catch (...) { THEMIS_WARN("executeAndEntitiesWithFallback: Deserialisierung fehlgeschlagen für PK={}", pk); }
	}
	span.setAttribute("query.entities_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(out)};
}

// ===== Range-aware Ausführung =====
namespace {
static inline size_t bigLimit() { return static_cast<size_t>(1000000000ULL); }
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeysRangeAware_(const ConjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndKeysRangeAware");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.range_count", static_cast<int64_t>(q.rangePredicates.size()));
	// 1) Hole Listen für alle Gleichheitsprädikate
	std::vector<std::vector<std::string>> lists;
	lists.reserve(q.predicates.size() + q.rangePredicates.size());

	for (const auto& p : q.predicates) {
		auto child = Tracer::startSpan("index.scanEqual");
		child.setAttribute("index.table", q.table);
		child.setAttribute("index.column", p.column);
		auto [st, keys] = secIdx_.scanKeysEqual(q.table, p.column, p.value);
		if (!st.ok) return {Status::Error(st.message), {}};
		std::sort(keys.begin(), keys.end());
		lists.push_back(std::move(keys));
		child.setAttribute("index.result_count", static_cast<int64_t>(lists.back().size()));
		child.setStatus(true);
	}

	// 2) Range-Prädikate
	for (const auto& r : q.rangePredicates) {
		if (!secIdx_.hasRangeIndex(q.table, r.column)) {
			return {Status::Error("Missing range index for column: " + r.column), {}};
		}
		auto child = Tracer::startSpan("index.scanRange");
		child.setAttribute("index.table", q.table);
		child.setAttribute("index.column", r.column);
		child.setAttribute("range.has_lower", r.lower.has_value());
		child.setAttribute("range.has_upper", r.upper.has_value());
		child.setAttribute("range.includeLower", r.includeLower);
		child.setAttribute("range.includeUpper", r.includeUpper);
		auto [st, keys] = secIdx_.scanKeysRange(q.table, r.column, r.lower, r.upper, r.includeLower, r.includeUpper, bigLimit(), false);
		if (!st.ok) return {Status::Error(st.message), {}};
		std::sort(keys.begin(), keys.end());
		lists.push_back(std::move(keys));
		child.setAttribute("index.result_count", static_cast<int64_t>(lists.back().size()));
		child.setStatus(true);
	}

	// Wenn keine Prädikate aber nur ORDER BY: initial candidates leer => special case
	std::vector<std::string> candidates;
	if (lists.empty()) {
		candidates.clear();
	} else {
		// 3) Schnittmenge
		candidates = intersectSortedLists_(std::move(lists));
	}

	// 4) ORDER BY
	if (q.orderBy.has_value()) {
		const auto& ob = q.orderBy.value();
		if (!secIdx_.hasRangeIndex(q.table, ob.column)) {
			return {Status::Error("ORDER BY requires range index on column: " + ob.column), {}};
		}
		// Bestimme Bounds aus passendem Range-Prädikat, falls vorhanden
		std::optional<std::string> lb; std::optional<std::string> ub; bool il=true, iu=true;
		for (const auto& r : q.rangePredicates) {
			if (r.column == ob.column) { lb = r.lower; ub = r.upper; il = r.includeLower; iu = r.includeUpper; break; }
		}
		// Erzeuge Kandidaten-Set für schnelles Membership-Checking (falls es Prädikate gab)
		std::unordered_set<std::string> candSet;
		if (!candidates.empty()) candSet.insert(candidates.begin(), candidates.end());

		std::vector<std::string> ordered;
		ordered.reserve(ob.limit);
		// Cursor-unterstützte Range-Scans: starte nach (value, pk), falls vorhanden
		std::optional<std::pair<std::string,std::string>> anchor;
		if (ob.cursor_value.has_value() && ob.cursor_pk.has_value()) {
			anchor = std::make_pair(ob.cursor_value.value(), ob.cursor_pk.value());
		}
		auto [st, scan] = secIdx_.scanKeysRangeAnchored(q.table, ob.column, lb, ub, il, iu, bigLimit(), ob.desc, anchor);
		if (!st.ok) return {Status::Error(st.message), {}};
		for (const auto& k : scan) {
			if (!candSet.empty() && candSet.find(k) == candSet.end()) continue; // filter
			ordered.push_back(k);
			if (ordered.size() >= ob.limit) break;
		}
		span.setAttribute("query.ordered_count", static_cast<int64_t>(ordered.size()));
		span.setStatus(true);
		return {Status::OK(), std::move(ordered)};
	}
	span.setAttribute("query.result_count", static_cast<int64_t>(candidates.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(candidates)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntitiesRangeAware_(const ConjunctiveQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeAndEntitiesRangeAware");
	span.setAttribute("query.table", q.table);
	auto [st, keys] = executeAndKeysRangeAware_(q);
	if (!st.ok) return {st, {}};
	std::vector<BaseEntity> out; out.reserve(keys.size());
	for (const auto& pk : keys) {
		auto blob = db_.get(q.table + ":" + pk);
		if (!blob) continue;
		try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
		catch (...) { THEMIS_WARN("executeAndEntitiesRangeAware_: Deserialisierung fehlgeschlagen für PK={}", pk); }
	}
	span.setAttribute("query.entities_count", static_cast<int64_t>(out.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(out)};
}

// ============================================================================
// Join/LET/COLLECT Support (MVP)
// ============================================================================

nlohmann::json QueryEngine::evaluateExpression(
	const std::shared_ptr<query::Expression>& expr,
	const EvaluationContext& ctx
) const {
	using namespace query;
	
	if (!expr) return nullptr;
	
	switch (expr->getType()) {
		case ASTNodeType::Literal: {
			auto lit = std::static_pointer_cast<LiteralExpr>(expr);
			return std::visit([](const auto& val) -> nlohmann::json {
				using T = std::decay_t<decltype(val)>;
				if constexpr (std::is_same_v<T, std::nullptr_t>) {
					return nullptr;
				} else if constexpr (std::is_same_v<T, bool>) {
					return val;
				} else if constexpr (std::is_same_v<T, int64_t>) {
					return val;
				} else if constexpr (std::is_same_v<T, double>) {
					return val;
				} else if constexpr (std::is_same_v<T, std::string>) {
					return val;
				}
				return nullptr;
			}, lit->value);
		}
		
		case ASTNodeType::Variable: {
			auto var = std::static_pointer_cast<VariableExpr>(expr);
			auto val = ctx.get(var->name);
			return val.value_or(nullptr);
		}
		
		case ASTNodeType::FieldAccess: {
			auto field = std::static_pointer_cast<FieldAccessExpr>(expr);
			auto obj = evaluateExpression(field->object, ctx);
			if (obj.is_object() && obj.contains(field->field)) {
				return obj[field->field];
			}
			return nullptr;
		}
		
		case ASTNodeType::BinaryOp: {
			auto binOp = std::static_pointer_cast<BinaryOpExpr>(expr);
			auto left = evaluateExpression(binOp->left, ctx);
			auto right = evaluateExpression(binOp->right, ctx);
			
			switch (binOp->op) {
				case BinaryOperator::Eq:  return left == right;
				case BinaryOperator::Neq: return left != right;
				case BinaryOperator::Lt:  return left < right;
				case BinaryOperator::Lte: return left <= right;
				case BinaryOperator::Gt:  return left > right;
				case BinaryOperator::Gte: return left >= right;
				case BinaryOperator::And: return left.get<bool>() && right.get<bool>();
				case BinaryOperator::Or:  return left.get<bool>() || right.get<bool>();
				case BinaryOperator::Add: 
					if (left.is_number() && right.is_number()) {
						return left.get<double>() + right.get<double>();
					}
					return nullptr;
				case BinaryOperator::Sub:
					if (left.is_number() && right.is_number()) {
						return left.get<double>() - right.get<double>();
					}
					return nullptr;
				case BinaryOperator::Mul:
					if (left.is_number() && right.is_number()) {
						return left.get<double>() * right.get<double>();
					}
					return nullptr;
				case BinaryOperator::Div:
					if (left.is_number() && right.is_number() && right.get<double>() != 0) {
						return left.get<double>() / right.get<double>();
					}
					return nullptr;
				default: return nullptr;
			}
		}
		
		case ASTNodeType::UnaryOp: {
			auto unOp = std::static_pointer_cast<UnaryOpExpr>(expr);
			auto operand = evaluateExpression(unOp->operand, ctx);
			
			switch (unOp->op) {
				case UnaryOperator::Not:   return !operand.get<bool>();
				case UnaryOperator::Minus: return -operand.get<double>();
				case UnaryOperator::Plus:  return operand.get<double>();
				default: return nullptr;
			}
		}
		
		case ASTNodeType::ObjectConstruct: {
			auto objConst = std::static_pointer_cast<ObjectConstructExpr>(expr);
			nlohmann::json obj = nlohmann::json::object();
			for (const auto& [key, valExpr] : objConst->fields) {
				obj[key] = evaluateExpression(valExpr, ctx);
			}
			return obj;
		}
		
		case ASTNodeType::ArrayLiteral: {
			auto arrLit = std::static_pointer_cast<ArrayLiteralExpr>(expr);
			nlohmann::json arr = nlohmann::json::array();
			for (const auto& elemExpr : arrLit->elements) {
				arr.push_back(evaluateExpression(elemExpr, ctx));
			}
			return arr;
		}

		case ASTNodeType::FunctionCall: {
			auto fn = std::static_pointer_cast<FunctionCallExpr>(expr);
			std::string name = fn->name;
			std::transform(name.begin(), name.end(), name.begin(), ::tolower);
			// BM25(doc): Liefert den Score für das aktuell gebundene Dokument
			if (name == "bm25") {
				if (fn->arguments.size() != 1) {
					return 0.0; // Falsche Arity -> neutral 0.0
				}
				// Argument kann Variable oder Ausdruck sein, der zu einem Objekt mit _key aufgelöst wird
				auto obj = evaluateExpression(fn->arguments[0], ctx);
				if (obj.is_object()) {
					std::string pk;
					if (obj.contains("_key") && obj["_key"].is_string()) {
						pk = obj["_key"].get<std::string>();
					} else if (obj.contains("_pk") && obj["_pk"].is_string()) {
						pk = obj["_pk"].get<std::string>();
					}
					if (!pk.empty()) {
						double s = ctx.getBm25ScoreForPk(pk);
						return s;
					}
				}
				return 0.0;
			}
			// FULLTEXT_SCORE(): Alias ohne Argument, versucht "doc" aus dem Kontext
			if (name == "fulltext_score") {
				// Best-Effort: nutze Variable "doc" falls vorhanden
				auto it = ctx.bindings.find("doc");
				if (it != ctx.bindings.end() && it->second.is_object() && it->second.contains("_key") && it->second["_key"].is_string()) {
					return ctx.getBm25ScoreForPk(it->second["_key"].get<std::string>());
				}
				return 0.0;
			}

			// SHORTESTPATH(start, target [, graph_id])
			if (name == "shortestpath") {
				// Arity 2 or 3
				if (fn->arguments.size() != 2 && fn->arguments.size() != 3) {
					return nullptr;
				}
				if (!graphIdx_) return nullptr; // Graph subsystem not available
				// Evaluate start and target
				auto a0 = evaluateExpression(fn->arguments[0], ctx);
				auto a1 = evaluateExpression(fn->arguments[1], ctx);
				if (!a0.is_string() || !a1.is_string()) return nullptr;
				std::string start = a0.get<std::string>();
				std::string target = a1.get<std::string>();
				std::string graph_id = "";
				if (fn->arguments.size() == 3) {
					auto a2 = evaluateExpression(fn->arguments[2], ctx);
					if (!a2.is_string()) return nullptr;
					graph_id = a2.get<std::string>();
				}
				// Call dijkstra (optionally with graph scope)
				std::pair<GraphIndexManager::Status, GraphIndexManager::PathResult> pres;
				if (graph_id.empty()) {
					pres = graphIdx_->dijkstra(start, target);
				} else {
					pres = graphIdx_->dijkstra(start, target, std::string_view(""), graph_id);
				}
				if (!pres.first.ok) return nullptr;
				nlohmann::json out = nlohmann::json::object();
				out["vertices"] = pres.second.path;
				out["totalCost"] = pres.second.totalCost;
				// Resolve edge IDs between successive vertices using outAdjacency
				nlohmann::json edges = nlohmann::json::array();
				const auto& path = pres.second.path;
				for (size_t i = 0; i + 1 < path.size(); ++i) {
					auto from = path[i];
					auto to = path[i+1];
					auto adjPair = graphIdx_->outAdjacency(from);
					if (!adjPair.first.ok) {
						edges.push_back(nullptr);
						continue;
					}
					bool found = false;
					for (const auto& ai : adjPair.second) {
						if (ai.targetPk == to) {
							edges.push_back(ai.edgeId);
							found = true;
							break;
						}
					}
					if (!found) edges.push_back(nullptr);
				}
				out["edges"] = edges;
				return out;
			}
			// Unbekannte Funktion im MVP: kein Wert
			return nullptr;
		}
		
		default:
			return nullptr;
	}
}

bool QueryEngine::evaluateCondition(
	const std::shared_ptr<query::Expression>& expr,
	const EvaluationContext& ctx
) const {
	auto result = evaluateExpression(expr, ctx);
	if (result.is_boolean()) {
		return result.get<bool>();
	}
	return false;
}

// Helper: Extract all variable names referenced in an expression
static void collectVariables(
	const std::shared_ptr<query::Expression>& expr,
	std::set<std::string>& vars
) {
	if (!expr) return;
	
	switch (expr->getType()) {
		case query::ASTNodeType::Variable: {
			auto varExpr = std::static_pointer_cast<query::VariableExpr>(expr);
			vars.insert(varExpr->name);
			break;
		}
		case query::ASTNodeType::FieldAccess: {
			auto fa = std::static_pointer_cast<query::FieldAccessExpr>(expr);
			collectVariables(fa->object, vars);
			break;
		}
		case query::ASTNodeType::BinaryOp: {
			auto bin = std::static_pointer_cast<query::BinaryOpExpr>(expr);
			collectVariables(bin->left, vars);
			collectVariables(bin->right, vars);
			break;
		}
		case query::ASTNodeType::UnaryOp: {
			auto un = std::static_pointer_cast<query::UnaryOpExpr>(expr);
			collectVariables(un->operand, vars);
			break;
		}
		case query::ASTNodeType::FunctionCall: {
			auto fn = std::static_pointer_cast<query::FunctionCallExpr>(expr);
			for (const auto& arg : fn->arguments) {
				collectVariables(arg, vars);
			}
			break;
		}
		case query::ASTNodeType::ArrayLiteral: {
			auto arr = std::static_pointer_cast<query::ArrayLiteralExpr>(expr);
			for (const auto& elem : arr->elements) {
				collectVariables(elem, vars);
			}
			break;
		}
		case query::ASTNodeType::ObjectConstruct: {
			auto obj = std::static_pointer_cast<query::ObjectConstructExpr>(expr);
			for (const auto& [key, val] : obj->fields) {
				collectVariables(val, vars);
			}
			break;
		}
		default:
			// Literal: no variables
			break;
	}
}

// Helper: Analyze if filters contain equi-join condition for hash-join optimization
struct EquiJoinCondition {
	bool found = false;
	std::string left_var;
	std::string left_field;
	std::string right_var;
	std::string right_field;
};

static EquiJoinCondition analyzeEquiJoin(
	const std::vector<std::shared_ptr<query::FilterNode>>& filters,
	const std::string& var1,
	const std::string& var2
) {
	EquiJoinCondition result;
	
	for (const auto& filter : filters) {
		auto expr = filter->condition;
		if (expr->getType() != query::ASTNodeType::BinaryOp) continue;
		
		auto bin = std::static_pointer_cast<query::BinaryOpExpr>(expr);
		if (bin->op != query::BinaryOperator::Eq) continue;
		
		// Check if left is var.field and right is var.field
		auto checkFieldAccess = [](const std::shared_ptr<query::Expression>& e) -> std::pair<std::string, std::string> {
			if (e->getType() != query::ASTNodeType::FieldAccess) return {"", ""};
			auto fa = std::static_pointer_cast<query::FieldAccessExpr>(e);
			if (fa->object->getType() != query::ASTNodeType::Variable) return {"", ""};
			auto varExpr = std::static_pointer_cast<query::VariableExpr>(fa->object);
			return {varExpr->name, fa->field};
		};
		
		auto [lvar, lfield] = checkFieldAccess(bin->left);
		auto [rvar, rfield] = checkFieldAccess(bin->right);
		
		if (lvar.empty() || rvar.empty()) continue;
		
		// Check if this matches our two variables
		if ((lvar == var1 && rvar == var2) || (lvar == var2 && rvar == var1)) {
			result.found = true;
			result.left_var = lvar;
			result.left_field = lfield;
			result.right_var = rvar;
			result.right_field = rfield;
			return result;
		}
	}
	
	return result;
}

std::pair<QueryEngine::Status, std::vector<nlohmann::json>> QueryEngine::executeJoin(
	const std::vector<query::ForNode>& for_nodes,
	const std::vector<std::shared_ptr<query::FilterNode>>& filters,
	const std::vector<query::LetNode>& let_nodes,
	const std::shared_ptr<query::ReturnNode>& return_node,
	const std::shared_ptr<query::SortNode>& sort,
	const std::shared_ptr<query::LimitNode>& limit
) const {
	auto span = Tracer::startSpan("QueryEngine.executeJoin");
	span.setAttribute("join.for_count", static_cast<int64_t>(for_nodes.size()));
	
	if (for_nodes.empty()) {
		return {Status::Error("executeJoin: No FOR clauses"), {}};
	}
	
	std::vector<nlohmann::json> results;
	
	// OPTIMIZATION: Predicate Push-down
	// Classify filters by which variables they reference
	std::map<std::string, std::vector<std::shared_ptr<query::FilterNode>>> single_var_filters;
	std::vector<std::shared_ptr<query::FilterNode>> multi_var_filters;
	
	for (const auto& filter : filters) {
		std::set<std::string> vars;
		collectVariables(filter->condition, vars);
		
		if (vars.size() == 1) {
			// Single-variable filter: can be pushed down
			single_var_filters[*vars.begin()].push_back(filter);
			span.setAttribute("join.pushed_filters", static_cast<int64_t>(single_var_filters.size()));
		} else {
			// Multi-variable filter: must be applied after join
			multi_var_filters.push_back(filter);
		}
	}
	
	// OPTIMIZATION: Hash-Join for 2-way equi-joins
	if (for_nodes.size() == 2) {
		auto equiJoin = analyzeEquiJoin(multi_var_filters, for_nodes[0].variable, for_nodes[1].variable);
		if (equiJoin.found) {
			span.setAttribute("join.algorithm", "hash_join");
			THEMIS_DEBUG("Using hash-join for {} = {}", 
				equiJoin.left_var + "." + equiJoin.left_field,
				equiJoin.right_var + "." + equiJoin.right_field);
			
			// Build phase: Create hash table from first collection
			std::unordered_map<std::string, std::vector<nlohmann::json>> hash_table;
			const auto& build_for = for_nodes[0];
			const std::string build_prefix = build_for.collection + ":";
			
			// Apply pushed-down filters for build side
			auto build_filters = single_var_filters.find(build_for.variable);
			
			db_.scanPrefix(build_prefix, [&](std::string_view key, std::string_view value) -> bool {
				std::string pk = KeySchema::extractPrimaryKey(key);
				std::vector<uint8_t> blob(value.begin(), value.end());
				try {
					BaseEntity entity = BaseEntity::deserialize(pk, blob);
					nlohmann::json doc = nlohmann::json::parse(entity.toJson());
					doc["_key"] = pk;
					
					// Apply pushed-down filters
					if (build_filters != single_var_filters.end()) {
						EvaluationContext filter_ctx;
						filter_ctx.bind(build_for.variable, doc);
						bool pass = true;
						for (const auto& filter : build_filters->second) {
							if (!evaluateCondition(filter->condition, filter_ctx)) {
								pass = false;
								break;
							}
						}
						if (!pass) return true; // Skip this document
					}
					
					// Extract join key
					std::string join_key_field = (equiJoin.left_var == build_for.variable) 
						? equiJoin.left_field 
						: equiJoin.right_field;
					
					if (doc.contains(join_key_field)) {
						std::string join_key = doc[join_key_field].dump();
						hash_table[join_key].push_back(doc);
					}
				} catch (...) {}
				return true;
			});
			
			// Probe phase: Scan second collection and probe hash table
			const auto& probe_for = for_nodes[1];
			const std::string probe_prefix = probe_for.collection + ":";
			
			// Apply pushed-down filters for probe side
			auto probe_filters = single_var_filters.find(probe_for.variable);
			
			db_.scanPrefix(probe_prefix, [&](std::string_view key, std::string_view value) -> bool {
				std::string pk = KeySchema::extractPrimaryKey(key);
				std::vector<uint8_t> blob(value.begin(), value.end());
				try {
					BaseEntity entity = BaseEntity::deserialize(pk, blob);
					nlohmann::json doc = nlohmann::json::parse(entity.toJson());
					doc["_key"] = pk;
					
					// Apply pushed-down filters
					if (probe_filters != single_var_filters.end()) {
						EvaluationContext filter_ctx;
						filter_ctx.bind(probe_for.variable, doc);
						bool pass = true;
						for (const auto& filter : probe_filters->second) {
							if (!evaluateCondition(filter->condition, filter_ctx)) {
								pass = false;
								break;
							}
						}
						if (!pass) return true; // Skip this document
					}
					
					// Extract join key
					std::string join_key_field = (equiJoin.right_var == probe_for.variable)
						? equiJoin.right_field
						: equiJoin.left_field;
					
					if (!doc.contains(join_key_field)) return true;
					std::string join_key = doc[join_key_field].dump();
					
					// Probe hash table
					auto it = hash_table.find(join_key);
					if (it == hash_table.end()) return true;
					
					// For each matching row from build side
					for (const auto& build_doc : it->second) {
						EvaluationContext ctx;
						ctx.bind(build_for.variable, build_doc);
						ctx.bind(probe_for.variable, doc);
						
						// Process LET bindings using LetEvaluator
						query::LetEvaluator letEval;
						nlohmann::json currentDoc;
						currentDoc[build_for.variable] = build_doc;
						currentDoc[probe_for.variable] = doc;
						
						for (const auto& let : let_nodes) {
							if (!letEval.evaluateLet(let, currentDoc)) {
								THEMIS_WARN("LET evaluation failed for variable '{}' in hash-join", let.variable);
								continue; // Skip this join result
							}
							auto letVal = letEval.resolveVariable(let.variable);
							if (letVal.has_value()) {
								ctx.bind(let.variable, letVal.value());
								currentDoc[let.variable] = letVal.value(); // For subsequent LETs
							}
						}
						
						// Apply remaining multi-variable FILTER conditions (excluding join condition)
						bool passFilters = true;
						for (const auto& filter : multi_var_filters) {
							// Skip the equi-join filter (already handled)
							if (filter->condition->getType() == query::ASTNodeType::BinaryOp) {
								auto bin = std::static_pointer_cast<query::BinaryOpExpr>(filter->condition);
								if (bin->op == query::BinaryOperator::Eq) {
									// Check if this is the join condition we already handled
									auto checkMatch = [&]() {
										if (bin->left->getType() != query::ASTNodeType::FieldAccess) return false;
										if (bin->right->getType() != query::ASTNodeType::FieldAccess) return false;
										auto lfa = std::static_pointer_cast<query::FieldAccessExpr>(bin->left);
										auto rfa = std::static_pointer_cast<query::FieldAccessExpr>(bin->right);
										if (lfa->object->getType() != query::ASTNodeType::Variable) return false;
										if (rfa->object->getType() != query::ASTNodeType::Variable) return false;
										auto lvar = std::static_pointer_cast<query::VariableExpr>(lfa->object)->name;
										auto rvar = std::static_pointer_cast<query::VariableExpr>(rfa->object)->name;
										return (lvar == equiJoin.left_var && lfa->field == equiJoin.left_field &&
										        rvar == equiJoin.right_var && rfa->field == equiJoin.right_field);
									};
									if (checkMatch()) continue; // Skip this filter
								}
							}
							
							if (!evaluateCondition(filter->condition, ctx)) {
								passFilters = false;
								break;
							}
						}
						
						if (!passFilters) continue;
						
						// Evaluate RETURN expression
						if (return_node) {
							auto result = evaluateExpression(return_node->expression, ctx);
							results.push_back(std::move(result));
						}
					}
				} catch (...) {}
				return true;
			});
			
			// Apply SORT/LIMIT and return
			goto apply_sort_limit;
		}
	}
	
	// FALLBACK: Nested-loop join implementation
	{
		span.setAttribute("join.algorithm", "nested_loop");

		// FOR var1 IN coll1 FOR var2 IN coll2 FILTER var1.x == var2.y RETURN {v1: var1, v2: var2}
		
		std::function<void(size_t, EvaluationContext)> nestedLoop;
		nestedLoop = [&](size_t depth, EvaluationContext ctx) {
			if (depth >= for_nodes.size()) {
				// Process LET bindings using LetEvaluator
				query::LetEvaluator letEval;
				nlohmann::json currentDoc; // Aggregate all bindings for LET evaluation
				for (const auto& [var, val] : ctx.bindings) {
					currentDoc[var] = val;
				}
				
				for (const auto& let : let_nodes) {
					// Evaluate LET with LetEvaluator for proper variable resolution
					if (!letEval.evaluateLet(let, currentDoc)) {
						THEMIS_WARN("LET evaluation failed for variable '{}', skipping result", let.variable);
						return;
					}
					// Bind LET variable to context for downstream use
					auto letVal = letEval.resolveVariable(let.variable);
					if (letVal.has_value()) {
						ctx.bind(let.variable, letVal.value());
						currentDoc[let.variable] = letVal.value(); // Update for subsequent LETs
					}
				}
				
				// Apply multi-variable FILTER conditions
				bool passFilters = true;
				for (const auto& filter : multi_var_filters) {
					if (!evaluateCondition(filter->condition, ctx)) {
						passFilters = false;
						break;
					}
				}
				
				if (!passFilters) return;
				
				// Evaluate RETURN expression
				if (return_node) {
					auto result = evaluateExpression(return_node->expression, ctx);
					results.push_back(std::move(result));
				}
				return;
			}
			
			// Load all documents from current collection
			const auto& for_node = for_nodes[depth];
			const std::string prefix = for_node.collection + ":";
			
			// Get pushed-down filters for this variable
			auto push_filters = single_var_filters.find(for_node.variable);
			
			db_.scanPrefix(prefix, [&](std::string_view key, std::string_view value) -> bool {
				// Extract PK
				std::string pk = KeySchema::extractPrimaryKey(key);
				
				// Deserialize entity
				std::vector<uint8_t> blob(value.begin(), value.end());
				try {
					BaseEntity entity = BaseEntity::deserialize(pk, blob);
					nlohmann::json doc = nlohmann::json::parse(entity.toJson());
					doc["_key"] = pk;
					
					// Apply pushed-down filters
					if (push_filters != single_var_filters.end()) {
						EvaluationContext filter_ctx = ctx;
						filter_ctx.bind(for_node.variable, doc);
						bool pass = true;
						for (const auto& filter : push_filters->second) {
							if (!evaluateCondition(filter->condition, filter_ctx)) {
								pass = false;
								break;
							}
						}
						if (!pass) return true; // Skip this document
					}
					
					// Bind current variable
					EvaluationContext newCtx = ctx;
					newCtx.bind(for_node.variable, doc);
					
					// Recurse to next FOR level
					nestedLoop(depth + 1, newCtx);
				} catch (...) {
					// Skip malformed entities
				}
				return true; // Continue iteration
			});
		};
		
		EvaluationContext rootCtx;
		nestedLoop(0, rootCtx);
	}
	
apply_sort_limit:
	// Apply SORT if specified
	if (sort && !sort->specifications.empty()) {
		const auto& spec = sort->specifications[0];
		std::sort(results.begin(), results.end(), [&](const nlohmann::json& a, const nlohmann::json& b) {
			EvaluationContext ctxA, ctxB;
			ctxA.bindings["doc"] = a;
			ctxB.bindings["doc"] = b;
			auto valA = evaluateExpression(spec.expression, ctxA);
			auto valB = evaluateExpression(spec.expression, ctxB);
			return spec.ascending ? (valA < valB) : (valA > valB);
		});
	}
	
	// Apply LIMIT if specified
	if (limit) {
		size_t offset = static_cast<size_t>(limit->offset);
		size_t count = static_cast<size_t>(limit->count);
		if (offset >= results.size()) {
			results.clear();
		} else {
			size_t end = std::min(offset + count, results.size());
			results = std::vector<nlohmann::json>(
				results.begin() + offset,
				results.begin() + end
			);
		}
	}
	
	span.setAttribute("join.result_count", static_cast<int64_t>(results.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(results)};
}

std::pair<QueryEngine::Status, std::vector<nlohmann::json>> QueryEngine::executeGroupBy(
	const query::ForNode& for_node,
	const std::shared_ptr<query::CollectNode>& collect,
	const std::vector<std::shared_ptr<query::FilterNode>>& filters,
	const std::shared_ptr<query::ReturnNode>& return_node
) const {
	auto span = Tracer::startSpan("QueryEngine.executeGroupBy");
	
	if (!collect || collect->groups.empty()) {
		return {Status::Error("executeGroupBy: No GROUP BY clause"), {}};
	}
	
	// Hash-based grouping
	std::unordered_map<std::string, std::vector<nlohmann::json>> groups;
	
	// Scan collection
	const std::string prefix = for_node.collection + ":";
	
	db_.scanPrefix(prefix, [&](std::string_view key, std::string_view value) -> bool {
		std::string pk = KeySchema::extractPrimaryKey(key);
		std::vector<uint8_t> blob(value.begin(), value.end());
		
		try {
			BaseEntity entity = BaseEntity::deserialize(pk, blob);
			nlohmann::json doc = nlohmann::json::parse(entity.toJson());
			doc["_key"] = pk;
			
			EvaluationContext ctx;
			ctx.bind(for_node.variable, doc);
			
			// Apply FILTER conditions
			bool passFilters = true;
			for (const auto& filter : filters) {
				if (!evaluateCondition(filter->condition, ctx)) {
					passFilters = false;
					break;
				}
			}
			if (!passFilters) return true; // Continue to next document
			
			// Evaluate group key
			auto groupKey = evaluateExpression(collect->groups[0].second, ctx);
			std::string key_str = groupKey.dump();
			
			groups[key_str].push_back(doc);
		} catch (...) {
			// Skip malformed entities
		}
		return true; // Continue iteration
	});
	
	// Compute aggregations
	std::vector<nlohmann::json> results;
	
	for (const auto& [key_str, docs] : groups) {
		EvaluationContext ctx;
		
		// Bind group key variable
		if (!collect->groups.empty()) {
			auto groupKey = nlohmann::json::parse(key_str);
			ctx.bind(collect->groups[0].first, groupKey);
		}
		
		// Compute aggregations
		for (const auto& agg : collect->aggregations) {
			nlohmann::json aggValue = nullptr;
			
			std::string funcUpper = agg.funcName;
			std::transform(funcUpper.begin(), funcUpper.end(), funcUpper.begin(), ::toupper);
			
			if (funcUpper == "COUNT") {
				aggValue = static_cast<int64_t>(docs.size());
			} else if (funcUpper == "SUM") {
				double sum = 0.0;
				for (const auto& doc : docs) {
					EvaluationContext docCtx;
					docCtx.bind(for_node.variable, doc);
					auto val = evaluateExpression(agg.argument, docCtx);
					if (val.is_number()) {
						sum += val.get<double>();
					}
				}
				aggValue = sum;
			} else if (funcUpper == "AVG") {
				double sum = 0.0;
				int count = 0;
				for (const auto& doc : docs) {
					EvaluationContext docCtx;
					docCtx.bind(for_node.variable, doc);
					auto val = evaluateExpression(agg.argument, docCtx);
					if (val.is_number()) {
						sum += val.get<double>();
						count++;
					}
				}
				aggValue = (count > 0) ? (sum / count) : 0.0;
			} else if (funcUpper == "MIN") {
				double minVal = std::numeric_limits<double>::max();
				for (const auto& doc : docs) {
					EvaluationContext docCtx;
					docCtx.bind(for_node.variable, doc);
					auto val = evaluateExpression(agg.argument, docCtx);
					if (val.is_number()) {
						minVal = std::min(minVal, val.get<double>());
					}
				}
				aggValue = minVal;
			} else if (funcUpper == "MAX") {
				double maxVal = std::numeric_limits<double>::lowest();
				for (const auto& doc : docs) {
					EvaluationContext docCtx;
					docCtx.bind(for_node.variable, doc);
					auto val = evaluateExpression(agg.argument, docCtx);
					if (val.is_number()) {
						maxVal = std::max(maxVal, val.get<double>());
					}
				}
				aggValue = maxVal;
			}
			
			ctx.bind(agg.varName, std::move(aggValue));
		}
		
		// Evaluate RETURN expression
		if (return_node) {
			auto result = evaluateExpression(return_node->expression, ctx);
			results.push_back(std::move(result));
		}
	}
	
	span.setAttribute("groupby.group_count", static_cast<int64_t>(results.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(results)};
}

// Recursive Path Query Implementation (Multi-Hop Traversal with Temporal Support)
std::pair<QueryEngine::Status, std::vector<std::vector<std::string>>>
QueryEngine::executeRecursivePathQuery(const RecursivePathQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeRecursivePathQuery");
	span.setAttribute("query.start_node", q.start_node);
	span.setAttribute("query.end_node", q.end_node);
	span.setAttribute("query.max_depth", static_cast<int64_t>(q.max_depth));
	
	if (!graphIdx_) {
		return {Status::Error("GraphIndexManager nicht verfügbar"), {}};
	}
	
	if (q.start_node.empty()) {
		return {Status::Error("start_node darf nicht leer sein"), {}};
	}
	
	// Temporal filter setup
	std::optional<int64_t> timestamp_ms;
	if (q.valid_from.has_value() && q.valid_to.has_value()) {
		// Use midpoint of time window as query timestamp
		int64_t from = std::stoll(*q.valid_from);
		int64_t to = std::stoll(*q.valid_to);
		timestamp_ms = (from + to) / 2;
	} else if (q.valid_from.has_value()) {
		timestamp_ms = std::stoll(*q.valid_from);
	} else if (q.valid_to.has_value()) {
		timestamp_ms = std::stoll(*q.valid_to);
	}
	
	std::vector<std::vector<std::string>> allPaths;
	
	// If end_node is specified, use Dijkstra for single shortest path
	if (!q.end_node.empty()) {
		GraphIndexManager::PathResult pathResult;
		GraphIndexManager::Status st;
		
	// Use edge_type filtering if specified
	bool hasTypeFilter = !q.edge_type.empty();
	std::string graphId = q.graph_id.empty() ? std::string("default") : q.graph_id;
		
		if (timestamp_ms.has_value()) {
			auto [status, result] = graphIdx_->dijkstraAtTime(q.start_node, q.end_node, *timestamp_ms);
			st = status;
			pathResult = result;
		} else if (hasTypeFilter) {
			auto [status, result] = graphIdx_->dijkstra(q.start_node, q.end_node, q.edge_type, graphId);
			st = status;
			pathResult = result;
		} else {
			auto [status, result] = graphIdx_->dijkstra(q.start_node, q.end_node);
			st = status;
			pathResult = result;
		}
		
		// "No path found" is not an error, just an empty result
		if (!st.ok && st.message.find("Kein Pfad gefunden") == std::string::npos) {
			span.setStatus(false, st.message);
			return {Status::Error(st.message), {}};
		}
		
		// Graph + Geo: Apply spatial filter to path vertices
		if (!pathResult.path.empty() && q.spatial_constraint.has_value()) {
			auto spatialSpan = Tracer::startSpan("spatial_filter_path");
			const auto& sc = *q.spatial_constraint;
			
			std::vector<std::string> filteredPath;
			bool pathValid = true;
			
			// Batch load all vertices in path
			std::vector<std::string> vertexKeys;
			vertexKeys.reserve(pathResult.path.size());
			for (const auto& vertexPk : pathResult.path) {
				vertexKeys.push_back(vertexPk);
			}
			
			auto vertexDataList = db_.multiGet(vertexKeys);
			
			// Evaluate spatial filter for each vertex
			for (size_t i = 0; i < pathResult.path.size(); ++i) {
				const auto& vertexPk = pathResult.path[i];
				const auto& vertexDataOpt = vertexDataList[i];
				
				if (!vertexDataOpt.has_value()) {
					pathValid = false;
					break;
				}
				
				nlohmann::json vertex;
				try {
					std::string vertexData(vertexDataOpt->begin(), vertexDataOpt->end());
					vertex = nlohmann::json::parse(vertexData);
				} catch (...) {
					pathValid = false;
					break;
				}
				
				// Evaluate spatial filter
				EvaluationContext ctx;
				ctx.set("v", vertex);
				
				if (!evaluateCondition(sc.spatial_filter, ctx)) {
					pathValid = false; // Vertex outside spatial constraint
					break;
				}
				
				filteredPath.push_back(vertexPk);
			}
			
			if (pathValid && !filteredPath.empty()) {
				allPaths.push_back(std::move(filteredPath));
			}
			
			spatialSpan.setAttribute("path_valid", pathValid);
			spatialSpan.setAttribute("batch_loaded", static_cast<int64_t>(pathResult.path.size()));
			spatialSpan.setStatus(true);
		} else if (!pathResult.path.empty()) {
			allPaths.push_back(std::move(pathResult.path));
		}
	} else {
		// No end_node: BFS to find all reachable nodes up to max_depth
		std::vector<std::string> reachableNodes;
		GraphIndexManager::Status st;
		
	// Use edge_type filtering if specified
	bool hasTypeFilter = !q.edge_type.empty();
	std::string graphId = q.graph_id.empty() ? std::string("default") : q.graph_id;
		
		if (timestamp_ms.has_value()) {
			auto [status, nodes] = graphIdx_->bfsAtTime(q.start_node, *timestamp_ms, q.max_depth);
			st = status;
			reachableNodes = std::move(nodes);
		} else if (hasTypeFilter) {
			auto [status, nodes] = graphIdx_->bfs(q.start_node, static_cast<int>(q.max_depth), q.edge_type, graphId);
			st = status;
			reachableNodes = std::move(nodes);
		} else {
			auto [status, nodes] = graphIdx_->bfs(q.start_node, static_cast<int>(q.max_depth));
			st = status;
			reachableNodes = std::move(nodes);
		}
		
		if (!st.ok) {
			span.setStatus(false, st.message);
			return {Status::Error(st.message), {}};
		}
		
		// Graph + Geo: Apply spatial filter to reachable nodes
		if (q.spatial_constraint.has_value()) {
			auto spatialSpan = Tracer::startSpan("spatial_filter_nodes");
			const auto& sc = *q.spatial_constraint;
			std::vector<std::string> filteredNodes;
			
			// Batch load all reachable vertices
			auto vertexDataList = db_.multiGet(reachableNodes);
			
			// Evaluate spatial filter for each vertex
			for (size_t i = 0; i < reachableNodes.size(); ++i) {
				const auto& vertexPk = reachableNodes[i];
				const auto& vertexDataOpt = vertexDataList[i];
				
				if (!vertexDataOpt.has_value()) continue;
				
				nlohmann::json vertex;
				try {
					std::string vertexData(vertexDataOpt->begin(), vertexDataOpt->end());
					vertex = nlohmann::json::parse(vertexData);
				} catch (...) {
					continue;
				}
				
				// Evaluate spatial filter
				EvaluationContext ctx;
				ctx.set("v", vertex);
				
				if (evaluateCondition(sc.spatial_filter, ctx)) {
					filteredNodes.push_back(vertexPk);
				}
			}
			
			reachableNodes = std::move(filteredNodes);
			spatialSpan.setAttribute("filtered_nodes", static_cast<int64_t>(reachableNodes.size()));
			spatialSpan.setAttribute("batch_loaded", static_cast<int64_t>(vertexDataList.size()));
			spatialSpan.setStatus(true);
		}
		
		// For each reachable node, compute path from start
		// This is a simplified version - in production, BFS already tracks paths
		for (const auto& node : reachableNodes) {
			if (node != q.start_node) {
				// For now, return single-node paths (path reconstruction would require BFS modification)
				allPaths.push_back({q.start_node, node});
			}
		}
	}
	
	span.setAttribute("query.path_count", static_cast<int64_t>(allPaths.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(allPaths)};
}

// ============================================================================
// Hybrid Multi-Model Query Implementations
// ============================================================================

// Helper: Extract MBR from spatial filter expression for index optimization
static std::optional<utils::geo::MBR> extractBBoxFromFilter(
    const std::shared_ptr<themis::query::Expression>& expr
) {
    using namespace themis::query;
    if (!expr) return std::nullopt;
    
    // Handle ST_Within(geom, POLYGON(...))
    if (expr->getType() == ASTNodeType::FunctionCall) {
        auto funcCall = std::static_pointer_cast<FunctionCallExpr>(expr);
        
        if (funcCall->name == "ST_Within" && funcCall->arguments.size() == 2) {
            // Second argument should be ST_GeomFromText("POLYGON(...)")
            auto arg2 = funcCall->arguments[1];
            if (arg2->getType() == ASTNodeType::FunctionCall) {
                auto geomFunc = std::static_pointer_cast<FunctionCallExpr>(arg2);
                if (geomFunc->name == "ST_GeomFromText" && geomFunc->arguments.size() == 1) {
                    auto litExpr = geomFunc->arguments[0];
                    if (litExpr->getType() == ASTNodeType::Literal) {
                        auto lit = std::static_pointer_cast<LiteralExpr>(litExpr);
                        if (std::holds_alternative<std::string>(lit->value)) {
                            std::string wkt = std::get<std::string>(lit->value);
                            // Parse POLYGON((minx miny, maxx miny, maxx maxy, minx maxy, minx miny))
                            if (wkt.rfind("POLYGON", 0) == 0) {
                                size_t start = wkt.find("((");
                                size_t end = wkt.find("))");
                                if (start != std::string::npos && end != std::string::npos) {
                                    std::string coords = wkt.substr(start + 2, end - start - 2);
                                    std::replace(coords.begin(), coords.end(), ',', ' ');
                                    std::istringstream iss(coords);
                                    double minx = std::numeric_limits<double>::max();
                                    double miny = std::numeric_limits<double>::max();
                                    double maxx = std::numeric_limits<double>::lowest();
                                    double maxy = std::numeric_limits<double>::lowest();
                                    double x, y;
                                    while (iss >> x >> y) {
                                        minx = std::min(minx, x); miny = std::min(miny, y);
                                        maxx = std::max(maxx, x); maxy = std::max(maxy, y);
                                    }
                                    if (minx <= maxx && miny <= maxy) {
                                        return utils::geo::MBR{minx, miny, maxx, maxy};
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Handle ST_DWithin(geom, ST_Point(x,y), distance)
        if (funcCall->name == "ST_DWithin" && funcCall->arguments.size() == 3) {
            auto arg2 = funcCall->arguments[1];
            auto arg3 = funcCall->arguments[2];
            
            if (arg2->getType() == ASTNodeType::FunctionCall && arg3->getType() == ASTNodeType::Literal) {
                auto pointFunc = std::static_pointer_cast<FunctionCallExpr>(arg2);
                auto distLit = std::static_pointer_cast<LiteralExpr>(arg3);
                
                if (pointFunc->name == "ST_Point" && pointFunc->arguments.size() == 2) {
                    if (pointFunc->arguments[0]->getType() == ASTNodeType::Literal &&
                        pointFunc->arguments[1]->getType() == ASTNodeType::Literal) {
                        
                        auto xLit = std::static_pointer_cast<LiteralExpr>(pointFunc->arguments[0]);
                        auto yLit = std::static_pointer_cast<LiteralExpr>(pointFunc->arguments[1]);
                        
                        double x = std::holds_alternative<double>(xLit->value) ? std::get<double>(xLit->value) : 0.0;
                        double y = std::holds_alternative<double>(yLit->value) ? std::get<double>(yLit->value) : 0.0;
                        double d = std::holds_alternative<double>(distLit->value) ? std::get<double>(distLit->value) : 0.0;
                        
                        return utils::geo::MBR{x - d, y - d, x + d, y + d};
                    }
                }
            }
        }
    }
    
    return std::nullopt;
}

// Vector + Geo: Spatial-filtered ANN search
std::pair<QueryEngine::Status, std::vector<QueryEngine::VectorGeoResult>>
QueryEngine::executeVectorGeoQuery(const VectorGeoQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeVectorGeoQuery");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.k", static_cast<int64_t>(q.k));
	
	// Strategy: Two-phase filtering
	// 1. Execute spatial filter to get candidate PKs (whitelist)
	// 2. Execute vector search with whitelist
	
	std::vector<VectorGeoResult> results;
	
	if (!q.spatial_filter) {
		return {Status::Error("Vector+Geo query requires spatial_filter"), {}};
	}
	
	// Phase 1: Spatial pre-filtering
	// Get all entities from table and filter by spatial constraint
	auto child1 = Tracer::startSpan("phase1.spatial_filter");
	
	std::vector<std::string> spatialCandidates;
	std::unordered_map<std::string, nlohmann::json> entityCache;
	
	// Optimized: Use SpatialIndexManager if available
	if (spatialIdx_) {
		auto bbox = extractBBoxFromFilter(q.spatial_filter);
		if (bbox.has_value()) {
			child1.setAttribute("method", "spatial_index");
			child1.setAttribute("bbox_minx", bbox->minx);
			child1.setAttribute("bbox_miny", bbox->miny);
			child1.setAttribute("bbox_maxx", bbox->maxx);
			child1.setAttribute("bbox_maxy", bbox->maxy);
			
			// Use R-Tree range query
			auto indexResults = spatialIdx_->searchWithin(q.table, *bbox);
			
			// Load entities for candidates
			for (const auto& result : indexResults) {
				auto [getSt, entity] = db_.get(q.table + ":" + result.primary_key);
				if (!getSt.ok) continue;
				
				try {
					nlohmann::json doc = nlohmann::json::parse(entity);
					spatialCandidates.push_back(result.primary_key);
					entityCache[result.primary_key] = doc;
				} catch (...) {
					// Skip invalid JSON
				}
			}
			
			child1.setAttribute("spatial_candidates", static_cast<int64_t>(spatialCandidates.size()));
			child1.setStatus(true);
			
			if (spatialCandidates.empty()) {
				span.setAttribute("result_count", 0);
				span.setStatus(true);
				return {Status::OK(), {}};
			}
			
			// Skip to Phase 2 with optimized candidates
			goto phase2_vector_search;
		} else {
			THEMIS_WARN("SpatialIndexManager available but could not extract BBox from filter, falling back to full scan");
		}
	}
	
	// Fallback: Full scan with spatial filter
	child1.setAttribute("method", "full_scan");
	auto it = db_.newIterator();
	std::string prefix = q.table + ":";
	it->Seek(prefix);
	
	while (it->Valid()) {
		std::string key(it->key().data(), it->key().size());
		if (key.rfind(prefix, 0) != 0) break;
		
		std::string pk = key.substr(prefix.length());
		std::string val(it->value().data(), it->value().size());
		
		try {
			nlohmann::json entity = nlohmann::json::parse(val);
			
			// Evaluate spatial filter
			EvaluationContext ctx;
			ctx.set("doc", entity);
			
			if (evaluateCondition(q.spatial_filter, ctx)) {
				spatialCandidates.push_back(pk);
				entityCache[pk] = entity;
			}
		} catch (...) {
			// Skip invalid JSON
		}
		
		it->Next();
	}
	
	child1.setAttribute("spatial_candidates", static_cast<int64_t>(spatialCandidates.size()));
	child1.setStatus(true);
	
	if (spatialCandidates.empty()) {
		span.setAttribute("result_count", 0);
		span.setStatus(true);
		return {Status::OK(), {}};
	}
	
phase2_vector_search:
	// Phase 2: Vector search with whitelist
	auto child2 = Tracer::startSpan("phase2.vector_search");
	
	// Optimized: Use VectorIndexManager if available
	if (vectorIdx_) {
		child2.setAttribute("method", "hnsw_with_whitelist");
		
		auto [st, indexResults] = vectorIdx_->searchKnn(q.query_vector, q.k, &spatialCandidates);
		if (st.ok) {
			// Convert VectorIndexManager::Result to VectorGeoResult
			for (const auto& r : indexResults) {
				VectorGeoResult vgr;
				vgr.pk = r.pk;
				vgr.vector_distance = r.distance;
				vgr.entity = entityCache[r.pk];  // Already cached from Phase 1
				results.push_back(std::move(vgr));
			}
			
			child2.setAttribute("vector_results", static_cast<int64_t>(results.size()));
			child2.setStatus(true);
			span.setAttribute("result_count", static_cast<int64_t>(results.size()));
			span.setStatus(true);
			return {Status::OK(), std::move(results)};
		} else {
			THEMIS_WARN("VectorIndexManager::searchKnn failed: " + st.message + ", falling back to brute-force");
		}
	}
	
	// Fallback: brute-force over spatial candidates if no VectorIndexManager
	child2.setAttribute("method", "brute_force");
	std::vector<std::pair<std::string, float>> vectorResults;
	
	for (const auto& pk : spatialCandidates) {
		const auto& entity = entityCache[pk];
		if (entity.contains(q.vector_field) && entity[q.vector_field].is_array()) {
			std::vector<float> vec = entity[q.vector_field].get<std::vector<float>>();
			
			// Compute L2 distance
			if (vec.size() == q.query_vector.size()) {
				float dist = 0.0f;
				for (size_t i = 0; i < vec.size(); ++i) {
					float diff = vec[i] - q.query_vector[i];
					dist += diff * diff;
				}
				dist = std::sqrt(dist);
				vectorResults.push_back({pk, dist});
			}
		}
	}
	
	// Sort by distance and take top-k
	std::sort(vectorResults.begin(), vectorResults.end(),
	          [](const auto& a, const auto& b) { return a.second < b.second; });
	
	size_t resultCount = std::min(vectorResults.size(), q.k);
	for (size_t i = 0; i < resultCount; ++i) {
		VectorGeoResult r;
		r.pk = vectorResults[i].first;
		r.vector_distance = vectorResults[i].second;
		r.entity = entityCache[vectorResults[i].first];
		results.push_back(std::move(r));
	}
	
	child2.setAttribute("vector_results", static_cast<int64_t>(results.size()));
	child2.setStatus(true);
	
	span.setAttribute("result_count", static_cast<int64_t>(results.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(results)};
}

// Content + Geo: Fulltext + Spatial hybrid search
std::pair<QueryEngine::Status, std::vector<QueryEngine::ContentGeoResult>>
QueryEngine::executeContentGeoQuery(const ContentGeoQuery& q) const {
	auto span = Tracer::startSpan("QueryEngine.executeContentGeoQuery");
	span.setAttribute("query.table", q.table);
	span.setAttribute("query.fulltext", q.fulltext_query);
	
	std::vector<ContentGeoResult> results;
	
	if (!q.spatial_filter) {
		return {Status::Error("Content+Geo query requires spatial_filter"), {}};
	}
	
	// Phase 1: Fulltext search
	auto child1 = Tracer::startSpan("phase1.fulltext_search");
	auto [st, ftResults] = secIdx_.scanFulltextWithScores(q.table, q.text_field, q.fulltext_query, q.limit);
	
	if (!st.ok) {
		child1.setStatus(false, st.message);
		span.setStatus(false, st.message);
		return {Status::Error(st.message), {}};
	}
	
	child1.setAttribute("fulltext_results", static_cast<int64_t>(ftResults.size()));
	child1.setStatus(true);
	
	if (ftResults.empty()) {
		span.setAttribute("result_count", 0);
		span.setStatus(true);
		return {Status::OK(), {}};
	}
	
	// Phase 2: Spatial filtering + optional distance boosting
	auto child2 = Tracer::startSpan("phase2.spatial_filter");
	
	for (const auto& [pk, bm25_score] : ftResults) {
		// Load entity
		auto [getSt, entity] = db_.get(q.table + ":" + pk);
		if (!getSt.ok) continue;
		
		nlohmann::json doc;
		try {
			doc = nlohmann::json::parse(entity);
		} catch (...) {
			continue;
		}
		
		// Evaluate spatial filter
		EvaluationContext ctx;
		ctx.set("doc", doc);
		
		if (!evaluateCondition(q.spatial_filter, ctx)) {
			continue; // Fails spatial constraint
		}
		
		ContentGeoResult r;
		r.pk = pk;
		r.bm25_score = bm25_score;
		r.entity = doc;
		
		// Optional: compute distance for boosting
		if (q.boost_by_distance && q.center_point.has_value()) {
			if (doc.contains(q.geom_field) && doc[q.geom_field].is_object()) {
				const auto& geom = doc[q.geom_field];
				if (geom.contains("type") && geom["type"] == "Point" && 
				    geom.contains("coordinates") && geom["coordinates"].is_array() && 
				    geom["coordinates"].size() >= 2) {
					
					double x = geom["coordinates"][0].get<double>();
					double y = geom["coordinates"][1].get<double>();
					double cx = (*q.center_point)[0];
					double cy = (*q.center_point)[1];
					
					double dx = x - cx;
					double dy = y - cy;
					r.geo_distance = std::sqrt(dx*dx + dy*dy);
				}
			}
		}
		
		results.push_back(std::move(r));
	}
	
	child2.setAttribute("spatial_results", static_cast<int64_t>(results.size()));
	child2.setStatus(true);
	
	// Optional: re-rank by combined score (BM25 + distance penalty)
	if (q.boost_by_distance) {
		std::sort(results.begin(), results.end(), [](const auto& a, const auto& b) {
			// Combined score: BM25 - distance_penalty (lower distance = higher rank)
			double score_a = a.bm25_score - (a.geo_distance.value_or(0.0) * 0.1);
			double score_b = b.bm25_score - (b.geo_distance.value_or(0.0) * 0.1);
			return score_a > score_b;
		});
	}
	
	span.setAttribute("result_count", static_cast<int64_t>(results.size()));
	span.setStatus(true);
	return {Status::OK(), std::move(results)};
}

} // namespace themis
