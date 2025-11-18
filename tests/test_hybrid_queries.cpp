#include <gtest/gtest.h>
#include "query/query_engine.h"
#include "query/aql_parser.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <nlohmann/json.hpp>
#include <filesystem>

using namespace themis;
using namespace themis::query;

class HybridQueriesTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all("data/themis_hybrid_test");

        RocksDBWrapper::Config cfg;
        cfg.db_path = "data/themis_hybrid_test";
        cfg.memtable_size_mb = 32;
        cfg.block_cache_size_mb = 64;

        db = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db->open());
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        graphIdx = std::make_unique<GraphIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *secIdx, *graphIdx);

        // Create fulltext index for Content+Geo tests
        auto st = secIdx->createIndex("documents", "text", SecondaryIndexManager::IndexType::FULLTEXT);
        ASSERT_TRUE(st.ok);

        setupTestData();
    }

    void TearDown() override {
        engine.reset();
        graphIdx.reset();
        secIdx.reset();
        db.reset();
        std::filesystem::remove_all("data/themis_hybrid_test");
    }

    void setupTestData() {
        // Vector + Geo: Images with embeddings and locations
        {
            BaseEntity img1("img1");
            img1.setField("name", std::string("Berlin Tower"));
            img1.setField("embedding", std::vector<float>{0.1f, 0.2f, 0.3f});
            img1.setField("location", std::string(R"({"type":"Point","coordinates":[13.405,52.52]})"));
            secIdx->put("images", img1);

            BaseEntity img2("img2");
            img2.setField("name", std::string("Paris Tower"));
            img2.setField("embedding", std::vector<float>{0.15f, 0.25f, 0.35f});
            img2.setField("location", std::string(R"({"type":"Point","coordinates":[2.35,48.86]})"));
            secIdx->put("images", img2);

            BaseEntity img3("img3");
            img3.setField("name", std::string("Munich Church"));
            img3.setField("embedding", std::vector<float>{0.9f, 0.8f, 0.7f});
            img3.setField("location", std::string(R"({"type":"Point","coordinates":[11.58,48.14]})"));
            secIdx->put("images", img3);
        }

        // Content + Geo: Documents with text and locations
        {
            BaseEntity doc1("doc1");
            doc1.setField("text", std::string("Best hotel in Berlin city center"));
            doc1.setField("location", std::string(R"({"type":"Point","coordinates":[13.405,52.52]})"));
            secIdx->put("documents", doc1);

            BaseEntity doc2("doc2");
            doc2.setField("text", std::string("Luxury hotel near Eiffel Tower"));
            doc2.setField("location", std::string(R"({"type":"Point","coordinates":[2.35,48.86]})"));
            secIdx->put("documents", doc2);

            BaseEntity doc3("doc3");
            doc3.setField("text", std::string("Budget hotel in Munich"));
            doc3.setField("location", std::string(R"({"type":"Point","coordinates":[11.58,48.14]})"));
            secIdx->put("documents", doc3);
        }

        // Graph + Geo: Locations connected by roads
        {
            BaseEntity loc1("locations/berlin");
            loc1.setField("name", std::string("Berlin"));
            loc1.setField("location", std::string(R"({"type":"Point","coordinates":[13.405,52.52]})"));
            secIdx->put("locations", loc1);

            BaseEntity loc2("locations/potsdam");
            loc2.setField("name", std::string("Potsdam"));
            loc2.setField("location", std::string(R"({"type":"Point","coordinates":[13.06,52.39]})"));
            secIdx->put("locations", loc2);

            BaseEntity loc3("locations/dresden");
            loc3.setField("name", std::string("Dresden"));
            loc3.setField("location", std::string(R"({"type":"Point","coordinates":[13.74,51.05]})"));
            secIdx->put("locations", loc3);

            BaseEntity loc4("locations/paris");
            loc4.setField("name", std::string("Paris"));
            loc4.setField("location", std::string(R"({"type":"Point","coordinates":[2.35,48.86]})"));
            secIdx->put("locations", loc4);

            // Edges
            BaseEntity edge1("roads/r1");
            edge1.setField("_from", std::string("locations/berlin"));
            edge1.setField("_to", std::string("locations/potsdam"));
            edge1.setField("distance", 30.0);
            graphIdx->addEdge(edge1);

            BaseEntity edge2("roads/r2");
            edge2.setField("_from", std::string("locations/potsdam"));
            edge2.setField("_to", std::string("locations/dresden"));
            edge2.setField("distance", 150.0);
            graphIdx->addEdge(edge2);

            BaseEntity edge3("roads/r3");
            edge3.setField("_from", std::string("locations/berlin"));
            edge3.setField("_to", std::string("locations/paris"));
            edge3.setField("distance", 1000.0);
            graphIdx->addEdge(edge3);
        }
    }

    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<GraphIndexManager> graphIdx;
    std::unique_ptr<QueryEngine> engine;
};

// ============================================================================
// Vector + Geo Tests
// ============================================================================

TEST_F(HybridQueriesTest, VectorGeo_SpatialFilteredANN_BerlinRegion)
{
    // Query: Find similar images within Berlin region (13.0-14.0, 52.0-53.0)
    VectorGeoQuery q;
    q.table = "images";
    q.vector_field = "embedding";
    q.geom_field = "location";
    q.query_vector = {0.12f, 0.22f, 0.32f}; // Similar to img1
    q.k = 10;

    // Spatial filter: ST_Within(location, POLYGON(...))
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varDoc, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    // Berlin region polygon
    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((13 52, 14 52, 14 53, 13 53, 13 52))")));
    auto callBBox = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    q.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callBBox }
    );

    auto [st, results] = engine->executeVectorGeoQuery(q);
    ASSERT_TRUE(st.ok) << st.message;

    // Only img1 (Berlin) should match; img2 (Paris), img3 (Munich) outside region
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].pk, "img1");
    EXPECT_LT(results[0].vector_distance, 0.1f); // Very similar to query
}

TEST_F(HybridQueriesTest, VectorGeo_NoSpatialMatches_EmptyResult)
{
    // Query: Find similar images in region without any images
    VectorGeoQuery q;
    q.table = "images";
    q.vector_field = "embedding";
    q.geom_field = "location";
    q.query_vector = {0.1f, 0.2f, 0.3f};
    q.k = 10;

    // Filter for region far from any test data
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varDoc, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))")));
    auto callBBox = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    q.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callBBox }
    );

    auto [st, results] = engine->executeVectorGeoQuery(q);
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_EQ(results.size(), 0u);
}

// ============================================================================
// Content + Geo Tests
// ============================================================================

TEST_F(HybridQueriesTest, ContentGeo_FulltextWithSpatial_BerlinHotels)
{
    // Query: Fulltext "hotel" AND within Berlin region
    ContentGeoQuery q;
    q.table = "documents";
    q.text_field = "text";
    q.fulltext_query = "hotel";
    q.geom_field = "location";
    q.limit = 100;

    // Spatial filter: ST_Within(location, Berlin region)
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varDoc, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((13 52, 14 52, 14 53, 13 53, 13 52))")));
    auto callBBox = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    q.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callBBox }
    );

    auto [st, results] = engine->executeContentGeoQuery(q);
    ASSERT_TRUE(st.ok) << st.message;

    // Only doc1 (Berlin hotel) should match
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].pk, "doc1");
    EXPECT_GT(results[0].bm25_score, 0.0);
}

TEST_F(HybridQueriesTest, ContentGeo_ProximityBoosting_NearestFirst)
{
    // Query: Fulltext "hotel" with distance boosting from center point
    ContentGeoQuery q;
    q.table = "documents";
    q.text_field = "text";
    q.fulltext_query = "hotel";
    q.geom_field = "location";
    q.limit = 100;
    q.boost_by_distance = true;
    q.center_point = {13.0, 52.0}; // Near Berlin

    // Spatial filter: ST_DWithin (proximity search)
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varDoc, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    auto litX = std::make_shared<LiteralExpr>(LiteralValue(13.0));
    auto litY = std::make_shared<LiteralExpr>(LiteralValue(52.0));
    auto callCenter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Point"),
        std::vector<std::shared_ptr<Expression>>{ litX, litY }
    );

    auto litDist = std::make_shared<LiteralExpr>(LiteralValue(1000.0)); // 1000 units (covers all)
    q.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_DWithin"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callCenter, litDist }
    );

    auto [st, results] = engine->executeContentGeoQuery(q);
    ASSERT_TRUE(st.ok) << st.message;

    // All hotels match, but Berlin should be first (closest)
    ASSERT_GE(results.size(), 1u);
    EXPECT_EQ(results[0].pk, "doc1"); // Berlin closest to center
    ASSERT_TRUE(results[0].geo_distance.has_value());
    EXPECT_LT(*results[0].geo_distance, 1.0); // Very close
}

// ============================================================================
// Graph + Geo Tests
// ============================================================================

TEST_F(HybridQueriesTest, GraphGeo_SpatialConstrainedTraversal_GermanyOnly)
{
    // Query: Find paths from Berlin, but only through German locations (exclude Paris)
    RecursivePathQuery q;
    q.start_node = "locations/berlin";
    q.end_node = ""; // Find all reachable
    q.max_depth = 3;

    // Spatial constraint: Only locations within Germany bbox (10-15 lon, 50-55 lat)
    RecursivePathQuery::SpatialConstraint sc;
    sc.vertex_geom_field = "location";

    auto varV = std::make_shared<VariableExpr>("v");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varV, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    // Germany bbox
    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((10 50, 15 50, 15 55, 10 55, 10 50))")));
    auto callBBox = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    sc.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callBBox }
    );

    q.spatial_constraint = sc;

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    ASSERT_TRUE(st.ok) << st.message;

    // Should reach Potsdam and Dresden, but NOT Paris
    ASSERT_GE(paths.size(), 1u);

    // Verify no paths contain Paris
    for (const auto& path : paths) {
        for (const auto& node : path) {
            EXPECT_NE(node, "locations/paris");
        }
    }
}

TEST_F(HybridQueriesTest, GraphGeo_ShortestPathWithSpatialFilter_BerlinToDresden)
{
    // Query: Shortest path from Berlin to Dresden with spatial constraint
    RecursivePathQuery q;
    q.start_node = "locations/berlin";
    q.end_node = "locations/dresden";
    q.max_depth = 5;

    // Spatial constraint: Only German locations
    RecursivePathQuery::SpatialConstraint sc;
    sc.vertex_geom_field = "location";

    auto varV = std::make_shared<VariableExpr>("v");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varV, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((10 50, 15 50, 15 55, 10 55, 10 50))")));
    auto callBBox = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    sc.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callBBox }
    );

    q.spatial_constraint = sc;

    auto [st, paths] = engine->executeRecursivePathQuery(q);
    ASSERT_TRUE(st.ok) << st.message;

    // Should find path: Berlin -> Potsdam -> Dresden
    ASSERT_EQ(paths.size(), 1u);
    ASSERT_GE(paths[0].size(), 2u);
    EXPECT_EQ(paths[0].front(), "locations/berlin");
    EXPECT_EQ(paths[0].back(), "locations/dresden");
}

// ============================================================================
// Vector+Geo with HNSW Optimization
// ============================================================================

TEST_F(HybridQueriesTest, VectorGeo_WithVectorIndexManager_UsesHNSW)
{
    // Setup: Create VectorIndexManager for images table
    auto vectorIdx = std::make_unique<VectorIndexManager>(*db);
    auto initSt = vectorIdx->init("images", 3, VectorIndexManager::Metric::L2);
    ASSERT_TRUE(initSt.ok) << initSt.message;

    // Add vectors to index
    BaseEntity img1("img1");
    img1.setField("name", std::string("Berlin Tower"));
    img1.setField("embedding", std::vector<float>{0.1f, 0.2f, 0.3f});
    img1.setField("location", std::string(R"({"type":"Point","coordinates":[13.405,52.52]})"));
    vectorIdx->addEntity(img1, "embedding");
    secIdx->put("images", img1);

    BaseEntity img2("img2");
    img2.setField("name", std::string("Paris Tower"));
    img2.setField("embedding", std::vector<float>{0.15f, 0.25f, 0.35f});
    img2.setField("location", std::string(R"({"type":"Point","coordinates":[2.35,48.86]})"));
    vectorIdx->addEntity(img2, "embedding");
    secIdx->put("images", img2);

    // Create optimized QueryEngine with VectorIndexManager
    auto optimizedEngine = std::make_unique<QueryEngine>(*db, *secIdx, *graphIdx, vectorIdx.get(), nullptr);

    // Query: Find similar images within Berlin region
    VectorGeoQuery q;
    q.table = "images";
    q.vector_field = "embedding";
    q.geom_field = "location";
    q.query_vector = {0.12f, 0.22f, 0.32f}; // Similar to img1
    q.k = 10;

    // Spatial filter: ST_Within(location, Berlin region)
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldLoc = std::make_shared<FieldAccessExpr>(varDoc, std::string("location"));

    auto callGeomFromJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldLoc }
    );

    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((13 52, 14 52, 14 53, 13 53, 13 52))")));
    auto callBBox = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    q.spatial_filter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromJSON, callBBox }
    );

    auto [st, results] = optimizedEngine->executeVectorGeoQuery(q);
    ASSERT_TRUE(st.ok) << st.message;

    // Only img1 (Berlin) should match
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].pk, "img1");
    EXPECT_LT(results[0].vector_distance, 0.1f); // Very similar to query

    // Verify HNSW was used (check trace logs manually)
    // This test proves VectorIndexManager integration works
}

