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

// ---------------- Vector+Geo Cost Model ----------------
QueryOptimizer::VectorGeoCostResult QueryOptimizer::chooseVectorGeoPlan(const VectorGeoCostInput& in) {
	// Tunable constants
	const double C_vec_base = 1.0;      // base cost per vector distance at dim=1
	const double C_spatial_eval = 0.15; // spatial predicate evaluation per candidate
	const double C_index_spatial = 0.02; // spatial index candidate fetch cost
	const double prefilterDiscountFactor = 0.65;

	double dimScale = static_cast<double>(in.vectorDim == 0 ? 128 : in.vectorDim) / 128.0;
	double C_vec = C_vec_base * dimScale;
	std::size_t universe = in.spatialIndexEntries ? in.spatialIndexEntries : 100000; // fallback
	if (in.prefilterSize > 0 && in.prefilterSize < universe) universe = in.prefilterSize;

	std::size_t spatialCandidates = static_cast<std::size_t>(universe * in.bboxRatio);
	if (spatialCandidates == 0) spatialCandidates = 1;

	// Spatial-first cost
	double spatialPhaseCost = in.hasSpatialIndex ? spatialCandidates * C_index_spatial : universe * C_spatial_eval;
	double vectorPhaseCostSpatialFirst = spatialCandidates * C_vec;
	double costSpatialFirst = spatialPhaseCost + vectorPhaseCostSpatialFirst;

	// Vector-first cost
	double vectorSearchCost;
	if (in.hasVectorIndex) {
		vectorSearchCost = std::log(static_cast<double>(universe) + 1.0) * dimScale; // ANN approximation
	} else {
		vectorSearchCost = universe * C_vec; // brute-force
	}
	std::size_t vectorCandidates = in.hasVectorIndex ? (in.k * in.overfetch) : universe;
	double spatialEvalAfterVector = vectorCandidates * C_spatial_eval;
	double costVectorFirst = vectorSearchCost + spatialEvalAfterVector;

	if (in.prefilterSize > 0 && in.prefilterSize < in.spatialIndexEntries * 0.1) {
		costVectorFirst *= prefilterDiscountFactor;
		costSpatialFirst *= prefilterDiscountFactor;
	}

	VectorGeoPlan plan = costVectorFirst < costSpatialFirst ? VectorGeoPlan::VectorThenSpatial : VectorGeoPlan::SpatialThenVector;
	return {plan, costSpatialFirst, costVectorFirst};
}

// ---------------- Content+Geo Cost Model (extended heuristic) ----------------
QueryOptimizer::ContentGeoCostResult QueryOptimizer::estimateContentGeo(const ContentGeoCostInput& in) {
	// Tunable constants
	const double C_fulltext_base = 1.0;      // base cost for fulltext scoring batch
	const double C_spatial_eval = 0.12;      // spatial predicate per candidate (no index)
	const double C_spatial_index = 0.02;     // spatial index candidate fetch cost
	const double smallBBoxBoost = 0.7;       // discount when bboxRatio very small (<1%)

	std::size_t hits = in.fulltextHits ? in.fulltextHits : in.limit; // fallback
	if (hits == 0) hits = 1;

	// Fulltext-first plan cost: FT scan + spatial evaluation of hits
	double ftPhase = C_fulltext_base * std::log(static_cast<double>(hits) + 5.0);
	double spatialPhase = hits * (in.hasSpatialIndex ? C_spatial_index : C_spatial_eval) * in.bboxRatio;
	double costFulltextThenSpatial = ftPhase + spatialPhase;

	// Spatial-first plan cost: spatial candidate fetch + naive token checks on candidates
	// Approximate spatial candidates as hits * bboxRatio (worst-case) or hits if bboxRatio>1.
	double spatialCandidates = std::max(1.0, static_cast<double>(hits) * in.bboxRatio);
	double spatialFetch = spatialCandidates * (in.hasSpatialIndex ? C_spatial_index : C_spatial_eval);
	// Naive fulltext eval: tokenize + set compare ~ O(tokens + docTokens) ~ approximated by constant * candidates
	double ftEvalCandidates = spatialCandidates * 0.25; // constant factor
	double costSpatialThenFulltext = spatialFetch + ftEvalCandidates;

	if (in.bboxRatio < 0.01) {
		costSpatialThenFulltext *= smallBBoxBoost; // very selective bbox favors spatial-first
	}

	bool chooseFT = costFulltextThenSpatial <= costSpatialThenFulltext;
	return {costFulltextThenSpatial, costSpatialThenFulltext, chooseFT};
}

// ---------------- Graph Path Cost Model (rough estimate) ----------------
QueryOptimizer::GraphPathCostResult QueryOptimizer::estimateGraphPath(const GraphPathCostInput& in) {
	double expanded = 1.0; // start node
	for (size_t d = 1; d <= in.maxDepth; ++d) {
		expanded += std::pow(in.branchingFactor, static_cast<int>(d));
	}
	if (in.hasSpatialConstraint) {
		expanded *= in.spatialSelectivity; // prune by spatial fraction
	}
	double timeMs = expanded * 0.02; // arbitrary scaling
	return {expanded, timeMs};
}

} // namespace themis
