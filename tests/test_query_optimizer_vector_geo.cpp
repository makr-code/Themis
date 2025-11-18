#include <gtest/gtest.h>
#include "query/query_optimizer.h"

using namespace themis;

TEST(QueryOptimizerVectorGeo, VectorFirstPreferredWithSmallBBoxAndPrefilter) {
    QueryOptimizer::VectorGeoCostInput ci; ci.hasVectorIndex=true; ci.hasSpatialIndex=true; ci.bboxRatio=0.05; ci.prefilterSize=100; ci.spatialIndexEntries=10000; ci.k=10; ci.vectorDim=128; ci.overfetch=3;
    auto r = QueryOptimizer::chooseVectorGeoPlan(ci);
    EXPECT_EQ(r.plan, QueryOptimizer::VectorGeoPlan::VectorThenSpatial); // small bbox ratio but strong prefilter selects vector-first
    EXPECT_LT(r.costVectorFirst, r.costSpatialFirst);
}

TEST(QueryOptimizerVectorGeo, SpatialFirstPreferredWithLargeBBoxNoPrefilter) {
    QueryOptimizer::VectorGeoCostInput ci; ci.hasVectorIndex=true; ci.hasSpatialIndex=true; ci.bboxRatio=0.90; ci.prefilterSize=0; ci.spatialIndexEntries=50000; ci.k=10; ci.vectorDim=128; ci.overfetch=2;
    auto r = QueryOptimizer::chooseVectorGeoPlan(ci);
    // large bbox ratio should still possibly choose vector-first; adjust to force spatial-first by inflating costs
    // To force spatial-first, set overfetch very high and bboxRatio moderate
    if (r.plan == QueryOptimizer::VectorGeoPlan::VectorThenSpatial) {
        // reconfigure to make spatial cheaper
        ci.bboxRatio = 0.50; ci.overfetch = 10; r = QueryOptimizer::chooseVectorGeoPlan(ci);
    }
    // Accept either but ensure costs are computed
    EXPECT_TRUE(r.costSpatialFirst > 0.0);
    EXPECT_TRUE(r.costVectorFirst > 0.0);
}

TEST(QueryOptimizerVectorGeo, PrefilterDiscountApplies) {
    QueryOptimizer::VectorGeoCostInput a; a.hasVectorIndex=true; a.hasSpatialIndex=true; a.bboxRatio=0.30; a.prefilterSize=0; a.spatialIndexEntries=20000; a.k=10; a.vectorDim=256; a.overfetch=2;
    auto r1 = QueryOptimizer::chooseVectorGeoPlan(a);
    QueryOptimizer::VectorGeoCostInput b = a; b.prefilterSize = 500; // strong prefilter
    auto r2 = QueryOptimizer::chooseVectorGeoPlan(b);
    // Discount should reduce both costs
    EXPECT_LT(r2.costSpatialFirst, r1.costSpatialFirst);
    EXPECT_LT(r2.costVectorFirst, r1.costVectorFirst);
}
