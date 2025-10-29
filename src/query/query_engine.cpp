// Parallel Query Engine implementation

#include "query/query_engine.h"
#include "query/query_optimizer.h"
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "storage/key_schema.h"
#include "utils/logger.h"

#include <tbb/parallel_invoke.h>
#include <tbb/task_group.h>
#include <algorithm>

namespace themis {

QueryEngine::QueryEngine(RocksDBWrapper& db, SecondaryIndexManager& secIdx)
	: db_(db), secIdx_(secIdx) {}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeys(const ConjunctiveQuery& q) const {
	if (q.table.empty()) return {Status::Error("executeAndKeys: table darf nicht leer sein"), {}};
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
			auto [st, keys] = secIdx_.scanKeysEqual(q.table, p.column, p.value);
			if (!st.ok) {
				THEMIS_ERROR("Parallel scan error ({}={}): {}", p.column, p.value, st.message);
				errors.push_back(st.message);
				return;
			}
			// Sortieren zur späteren Schnittmenge
			std::sort(keys.begin(), keys.end());
			all_lists[i] = std::move(keys);
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
	return {Status::OK(), std::move(keys)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntities(const ConjunctiveQuery& q) const {
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

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeysSequential(const std::string& table,
									  const std::vector<PredicateEq>& orderedPredicates) const {
	if (table.empty()) return {Status::Error("executeAndKeysSequential: table leer"), {}};
	if (orderedPredicates.empty()) return {Status::Error("executeAndKeysSequential: keine Prädikate"), {}};

	// Starte mit erster Liste
	auto [st0, base] = secIdx_.scanKeysEqual(table, orderedPredicates[0].column, orderedPredicates[0].value);
	if (!st0.ok) return {Status::Error("sequential: " + st0.message), {}};
	if (base.empty()) return {Status::OK(), {}};
	std::sort(base.begin(), base.end());

	std::vector<std::string> current = std::move(base);
	for (size_t i = 1; i < orderedPredicates.size(); ++i) {
		const auto& p = orderedPredicates[i];
		auto [st, keys] = secIdx_.scanKeysEqual(table, p.column, p.value);
		if (!st.ok) return {Status::Error("sequential: " + st.message), {}};
		if (keys.empty()) return {Status::OK(), {}};
		std::sort(keys.begin(), keys.end());
		std::vector<std::string> tmp;
		tmp.reserve(std::min(current.size(), keys.size()));
		std::set_intersection(current.begin(), current.end(), keys.begin(), keys.end(), std::back_inserter(tmp));
		current.swap(tmp);
		if (current.empty()) break;
	}
	return {Status::OK(), std::move(current)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntitiesSequential(const std::string& table,
										  const std::vector<PredicateEq>& orderedPredicates) const {
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

	return {Status::OK(), std::move(out)};
}

} // namespace themis

namespace themis {

std::vector<std::string> QueryEngine::fullScanAndFilter_(const ConjunctiveQuery& q) const {
	std::vector<std::string> out;
	if (q.table.empty()) return out;
	const std::string prefix = q.table + ":";
	
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
	return out;
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeysWithFallback(const ConjunctiveQuery& q, bool optimize) const {
	// If no predicates at all, must do full scan
	if (q.predicates.empty() && q.rangePredicates.empty() && !q.orderBy.has_value()) {
		auto keys = fullScanAndFilter_(q);
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
			return executeAndKeysRangeAware_(q);
		}
		if (optimize) {
			QueryOptimizer opt(secIdx_);
			auto plan = opt.chooseOrderForAndQuery(q);
			return executeAndKeysSequential(q.table, plan.orderedPredicates);
		}
		return executeAndKeys(q);
	}

	// Fallback: full scan (inkl. Range-Prädikate)
	auto keys = fullScanAndFilter_(q);
	return {Status::OK(), std::move(keys)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntitiesWithFallback(const ConjunctiveQuery& q, bool optimize) const {
	auto [st, keys] = executeAndKeysWithFallback(q, optimize);
	if (!st.ok) return {st, {}};
	std::vector<BaseEntity> out; out.reserve(keys.size());
	for (const auto& pk : keys) {
		auto blob = db_.get(q.table + ":" + pk);
		if (!blob) continue;
		try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
		catch (...) { THEMIS_WARN("executeAndEntitiesWithFallback: Deserialisierung fehlgeschlagen für PK={}", pk); }
	}
	return {Status::OK(), std::move(out)};
}

// ===== Range-aware Ausführung =====
namespace {
static inline size_t bigLimit() { return static_cast<size_t>(1000000000ULL); }
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryEngine::executeAndKeysRangeAware_(const ConjunctiveQuery& q) const {
	// 1) Hole Listen für alle Gleichheitsprädikate
	std::vector<std::vector<std::string>> lists;
	lists.reserve(q.predicates.size() + q.rangePredicates.size());

	for (const auto& p : q.predicates) {
		auto [st, keys] = secIdx_.scanKeysEqual(q.table, p.column, p.value);
		if (!st.ok) return {Status::Error(st.message), {}};
		std::sort(keys.begin(), keys.end());
		lists.push_back(std::move(keys));
	}

	// 2) Range-Prädikate
	for (const auto& r : q.rangePredicates) {
		if (!secIdx_.hasRangeIndex(q.table, r.column)) {
			return {Status::Error("Missing range index for column: " + r.column), {}};
		}
		auto [st, keys] = secIdx_.scanKeysRange(q.table, r.column, r.lower, r.upper, r.includeLower, r.includeUpper, bigLimit(), false);
		if (!st.ok) return {Status::Error(st.message), {}};
		std::sort(keys.begin(), keys.end());
		lists.push_back(std::move(keys));
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
		auto [st, scan] = secIdx_.scanKeysRange(q.table, ob.column, lb, ub, il, iu, bigLimit(), ob.desc);
		if (!st.ok) return {Status::Error(st.message), {}};
		for (const auto& k : scan) {
			if (!candSet.empty() && candSet.find(k) == candSet.end()) continue; // filter
			ordered.push_back(k);
			if (ordered.size() >= ob.limit) break;
		}
		return {Status::OK(), std::move(ordered)};
	}

	return {Status::OK(), std::move(candidates)};
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryEngine::executeAndEntitiesRangeAware_(const ConjunctiveQuery& q) const {
	auto [st, keys] = executeAndKeysRangeAware_(q);
	if (!st.ok) return {st, {}};
	std::vector<BaseEntity> out; out.reserve(keys.size());
	for (const auto& pk : keys) {
		auto blob = db_.get(q.table + ":" + pk);
		if (!blob) continue;
		try { out.emplace_back(BaseEntity::deserialize(pk, *blob)); }
		catch (...) { THEMIS_WARN("executeAndEntitiesRangeAware_: Deserialisierung fehlgeschlagen für PK={}", pk); }
	}
	return {Status::OK(), std::move(out)};
}

} // namespace themis
