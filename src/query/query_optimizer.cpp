// Cost-based Query Optimizer implementation

#include "query/query_optimizer.h"
#include "index/secondary_index.h"
#include "storage/base_entity.h"

#include <algorithm>
#include <numeric>

namespace themis {

QueryOptimizer::QueryOptimizer(SecondaryIndexManager& secIdx) : secIdx_(secIdx) {}

QueryOptimizer::Plan QueryOptimizer::chooseOrderForAndQuery(const ConjunctiveQuery& q, size_t maxProbePerPred) const {
	Plan plan;
	plan.orderedPredicates.reserve(q.predicates.size());
	plan.details.reserve(q.predicates.size());

	// Schätzung je Prädikat
	for (const auto& p : q.predicates) {
		bool capped = false;
		size_t cnt = secIdx_.estimateCountEqual(q.table, p.column, p.value, maxProbePerPred, &capped);
		plan.details.push_back(Estimation{p, cnt, capped});
	}

	// Sortiere Prädikate nach (capped? maxProbe : count) aufsteigend
	std::vector<size_t> idx(q.predicates.size());
	std::iota(idx.begin(), idx.end(), 0);
	std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b){
		auto va = plan.details[a];
		auto vb = plan.details[b];
		auto ea = va.capped ? maxProbePerPred : va.estimatedCount;
		auto eb = vb.capped ? maxProbePerPred : vb.estimatedCount;
		if (ea != eb) return ea < eb;
		return va.pred.column < vb.pred.column; // stabile Ordnung
	});

	for (auto i : idx) plan.orderedPredicates.push_back(plan.details[i].pred);
	return plan;
}

std::pair<QueryEngine::Status, std::vector<std::string>>
QueryOptimizer::executeOptimizedKeys(QueryEngine& engine, const ConjunctiveQuery& q, const Plan& plan) const {
	return engine.executeAndKeysSequential(q.table, plan.orderedPredicates);
}

std::pair<QueryEngine::Status, std::vector<BaseEntity>>
QueryOptimizer::executeOptimizedEntities(QueryEngine& engine, const ConjunctiveQuery& q, const Plan& plan) const {
	return engine.executeAndEntitiesSequential(q.table, plan.orderedPredicates);
}

} // namespace themis
