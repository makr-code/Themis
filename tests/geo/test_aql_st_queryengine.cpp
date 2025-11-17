#include <gtest/gtest.h>
#include "query/aql_parser.h"
#include "query/query_engine.h"
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <nlohmann/json.hpp>
#include <filesystem>

using namespace themis;
using namespace themis::query;

class AQLSTQueryEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all("data/themis_aql_st_test");

        RocksDBWrapper::Config cfg;
        cfg.db_path = "data/themis_aql_st_test";
        cfg.memtable_size_mb = 32;
        cfg.block_cache_size_mb = 64;

        db = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db->open());
        secIdx = std::make_unique<SecondaryIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *secIdx);

        // Insert documents into 'places' with geometry as GeoJSON string
        BaseEntity p1("p1");
        p1.setField("name", std::string("inside"));
        p1.setField("geom", std::string(R"({\"type\":\"Point\",\"coordinates\":[1.0,1.0]})"));
        secIdx->put("places", p1);

        BaseEntity p2("p2");
        p2.setField("name", std::string("outside"));
        p2.setField("geom", std::string(R"({\"type\":\"Point\",\"coordinates\":[10.0,10.0]})"));
        secIdx->put("places", p2);
    }

    void TearDown() override {
        engine.reset();
        secIdx.reset();
        db.reset();
        std::filesystem::remove_all("data/themis_aql_st_test");
    }

    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<SecondaryIndexManager> secIdx;
    std::unique_ptr<QueryEngine> engine;
};

// FILTER with ST_*: ST_Within(ST_GeomFromGeoJSON(doc.geom), ST_GeomFromText("POLYGON(...))")
TEST_F(AQLSTQueryEngineTest, Filter_ST_Within_GeoJSON_Field)
{
    // Build AST manually
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldGeom = std::make_shared<FieldAccessExpr>(varDoc, std::string("geom"));

    // Left arg: ST_GeomFromGeoJSON(doc.geom)
    auto callGeomFromGeoJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldGeom }
    );

    // Right arg: ST_GeomFromText("POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))")
    auto litWkt = std::make_shared<LiteralExpr>(LiteralValue(std::string("POLYGON((0 0, 2 0, 2 2, 0 2, 0 0))")));
    auto callGeomFromText = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromText"),
        std::vector<std::shared_ptr<Expression>>{ litWkt }
    );

    // Condition: ST_Within(left, right)
    auto cond = std::make_shared<FunctionCallExpr>(
        std::string("ST_Within"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromGeoJSON, callGeomFromText }
    );

    // Return: doc._key
    auto fieldKey = std::make_shared<FieldAccessExpr>(varDoc, std::string("_key"));

    // One FOR
    std::vector<ForNode> fors;
    ForNode f; f.variable = "doc"; f.collection = "places"; fors.push_back(f);

    // One FILTER
    auto filter = std::make_shared<FilterNode>(cond);
    std::vector<std::shared_ptr<FilterNode>> filters; filters.push_back(filter);

    // RETURN
    auto ret = std::make_shared<ReturnNode>(fieldKey);

    auto [st, results] = engine->executeJoin(fors, filters, {}, ret, nullptr, nullptr);
    ASSERT_TRUE(st.ok) << st.message;

    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].is_string());
    EXPECT_EQ(results[0].get<std::string>(), "p1");
}

// RETURN with ST_*: Return WKT of buffered point
TEST_F(AQLSTQueryEngineTest, Return_ST_AsText_Buffer_Result)
{
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldGeom = std::make_shared<FieldAccessExpr>(varDoc, std::string("geom"));

    auto callGeomFromGeoJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldGeom }
    );
    auto litDist = std::make_shared<LiteralExpr>(LiteralValue(1.0));
    auto callBuffer = std::make_shared<FunctionCallExpr>(
        std::string("ST_Buffer"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromGeoJSON, litDist }
    );
    auto callAsText = std::make_shared<FunctionCallExpr>(
        std::string("ST_AsText"),
        std::vector<std::shared_ptr<Expression>>{ callBuffer }
    );

    // FOR and FILTER to pick the inside point
    std::vector<ForNode> fors; ForNode f; f.variable="doc"; f.collection="places"; fors.push_back(f);

    // Filter doc.name == "inside"
    auto fieldName = std::make_shared<FieldAccessExpr>(varDoc, std::string("name"));
    auto litInside = std::make_shared<LiteralExpr>(LiteralValue(std::string("inside")));
    auto cond = std::make_shared<BinaryOpExpr>(BinaryOperator::Eq, fieldName, litInside);
    auto filter = std::make_shared<FilterNode>(cond);

    auto [st, results] = engine->executeJoin(fors, {filter}, {}, std::make_shared<ReturnNode>(callAsText), nullptr, nullptr);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].is_string());
    std::string wkt = results[0].get<std::string>();
    // Expect a POLYGON WKT (buffer result is polygon)
    ASSERT_FALSE(wkt.empty());
    ASSERT_EQ(wkt.rfind("POLYGON(", 0), 0u);
}

// FILTER with ST_DWithin: near (0,0) within radius 2 should pick only p1
TEST_F(AQLSTQueryEngineTest, Filter_ST_DWithin_GeoJSON_Field)
{
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldGeom = std::make_shared<FieldAccessExpr>(varDoc, std::string("geom"));

    auto callGeomFromGeoJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldGeom }
    );

    auto litX = std::make_shared<LiteralExpr>(LiteralValue(0.0));
    auto litY = std::make_shared<LiteralExpr>(LiteralValue(0.0));
    auto callCenter = std::make_shared<FunctionCallExpr>(
        std::string("ST_Point"),
        std::vector<std::shared_ptr<Expression>>{ litX, litY }
    );
    auto litRadius = std::make_shared<LiteralExpr>(LiteralValue(2.0));

    auto cond = std::make_shared<FunctionCallExpr>(
        std::string("ST_DWithin"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromGeoJSON, callCenter, litRadius }
    );

    // Return key
    auto fieldKey = std::make_shared<FieldAccessExpr>(varDoc, std::string("_key"));

    std::vector<ForNode> fors; ForNode f; f.variable = "doc"; f.collection = "places"; fors.push_back(f);
    auto filter = std::make_shared<FilterNode>(cond);
    auto [st, results] = engine->executeJoin(fors, {filter}, {}, std::make_shared<ReturnNode>(fieldKey), nullptr, nullptr);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].is_string());
    EXPECT_EQ(results[0].get<std::string>(), "p1");
}

// FILTER with ST_ZBetween: 3D point in Z-range
TEST_F(AQLSTQueryEngineTest, Filter_ST_ZBetween_3D_Point)
{
    // Insert 3D point
    BaseEntity p3("p3");
    p3.setField("name", std::string("elevated"));
    p3.setField("geom", std::string(R"({"type":"Point","coordinates":[1.0,1.0,50.0]})"));
    secIdx->put("places", p3);

    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldGeom = std::make_shared<FieldAccessExpr>(varDoc, std::string("geom"));

    auto callGeomFromGeoJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldGeom }
    );

    auto litZMin = std::make_shared<LiteralExpr>(LiteralValue(40.0));
    auto litZMax = std::make_shared<LiteralExpr>(LiteralValue(60.0));
    auto cond = std::make_shared<FunctionCallExpr>(
        std::string("ST_ZBetween"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromGeoJSON, litZMin, litZMax }
    );

    auto fieldKey = std::make_shared<FieldAccessExpr>(varDoc, std::string("_key"));

    std::vector<ForNode> fors; ForNode f; f.variable = "doc"; f.collection = "places"; fors.push_back(f);
    auto filter = std::make_shared<FilterNode>(cond);
    auto [st, results] = engine->executeJoin(fors, {filter}, {}, std::make_shared<ReturnNode>(fieldKey), nullptr, nullptr);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].get<std::string>(), "p3");
}

// RETURN with ST_Union: combine two geometries
TEST_F(AQLSTQueryEngineTest, Return_ST_Union_Two_Points)
{
    auto varDoc = std::make_shared<VariableExpr>("doc");
    auto fieldGeom = std::make_shared<FieldAccessExpr>(varDoc, std::string("geom"));

    auto callGeomFromGeoJSON = std::make_shared<FunctionCallExpr>(
        std::string("ST_GeomFromGeoJSON"),
        std::vector<std::shared_ptr<Expression>>{ fieldGeom }
    );

    auto litX = std::make_shared<LiteralExpr>(LiteralValue(5.0));
    auto litY = std::make_shared<LiteralExpr>(LiteralValue(5.0));
    auto callPoint = std::make_shared<FunctionCallExpr>(
        std::string("ST_Point"),
        std::vector<std::shared_ptr<Expression>>{ litX, litY }
    );

    auto callUnion = std::make_shared<FunctionCallExpr>(
        std::string("ST_Union"),
        std::vector<std::shared_ptr<Expression>>{ callGeomFromGeoJSON, callPoint }
    );

    // Filter doc.name == "inside"
    auto fieldName = std::make_shared<FieldAccessExpr>(varDoc, std::string("name"));
    auto litInside = std::make_shared<LiteralExpr>(LiteralValue(std::string("inside")));
    auto cond = std::make_shared<BinaryOpExpr>(BinaryOperator::Eq, fieldName, litInside);
    auto filter = std::make_shared<FilterNode>(cond);

    std::vector<ForNode> fors; ForNode f; f.variable="doc"; f.collection="places"; fors.push_back(f);
    auto [st, results] = engine->executeJoin(fors, {filter}, {}, std::make_shared<ReturnNode>(callUnion), nullptr, nullptr);
    ASSERT_TRUE(st.ok) << st.message;
    ASSERT_EQ(results.size(), 1u);
    ASSERT_TRUE(results[0].is_object());
    EXPECT_EQ(results[0]["type"], "Polygon");
}

