#include <gtest/gtest.h>
#include "index/spatial_index.h"
#include "api/geo_index_hooks.h"
#include "utils/geo/ewkb.h"
#include "storage/rocksdb_wrapper.h"
#include <memory>
#include <nlohmann/json.hpp>

using namespace themis;
using namespace themis::index;
using namespace themis::geo;
using namespace themis::api;
using json = nlohmann::json;

class GeoIndexIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create temporary RocksDB instance
        db_ = std::make_unique<RocksDBWrapper>("test_geo_integration.db");
        spatial_mgr_ = std::make_unique<SpatialIndexManager>(*db_);
        
        // Create spatial index for test table
        RTreeConfig config;
        config.total_bounds = MBR(-180.0, -90.0, 180.0, 90.0);
        auto status = spatial_mgr_->createSpatialIndex("test_points", "geometry", config);
        ASSERT_TRUE(status.ok());
    }
    
    void TearDown() override {
        spatial_mgr_.reset();
        db_.reset();
        std::remove("test_geo_integration.db");
    }
    
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SpatialIndexManager> spatial_mgr_;
};

// Test: Insert entity with GeoJSON polygon triggers spatial index insert
TEST_F(GeoIndexIntegrationTest, InsertPolygonTriggersIndexUpdate) {
    // Create a simple polygon (rectangle)
    json entity;
    entity["id"] = "poly1";
    entity["name"] = "Test Polygon";
    entity["geometry"] = {
        {"type", "Polygon"},
        {"coordinates", json::array({
            json::array({
                json::array({10.0, 50.0}),
                json::array({11.0, 50.0}),
                json::array({11.0, 51.0}),
                json::array({10.0, 51.0}),
                json::array({10.0, 50.0})
            })
        })}
    };
    
    std::string blob_str = entity.dump();
    std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
    
    // Call hook (simulating entity PUT)
    GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "poly1", blob);
    
    // Verify: Search for entities in bbox that overlaps polygon
    MBR query_box(10.0, 50.0, 11.0, 51.0);
    auto results = spatial_mgr_->searchIntersects("test_points", query_box);
    
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].primary_key, "poly1");
}

// Test: Query with exact backend returns correct results
TEST_F(GeoIndexIntegrationTest, SearchIntersectsWithExactCheck) {
    // Insert two polygons
    json poly1;
    poly1["id"] = "poly1";
    poly1["geometry"] = {
        {"type", "Polygon"},
        {"coordinates", json::array({
            json::array({
                json::array({10.0, 50.0}),
                json::array({10.5, 50.0}),
                json::array({10.5, 50.5}),
                json::array({10.0, 50.5}),
                json::array({10.0, 50.0})
            })
        })}
    };
    
    json poly2;
    poly2["id"] = "poly2";
    poly2["geometry"] = {
        {"type", "Polygon"},
        {"coordinates", json::array({
            json::array({
                json::array({20.0, 60.0}),
                json::array({20.5, 60.0}),
                json::array({20.5, 60.5}),
                json::array({20.0, 60.5}),
                json::array({20.0, 60.0})
            })
        })}
    };
    
    std::string blob1 = poly1.dump();
    std::string blob2 = poly2.dump();
    std::vector<uint8_t> b1(blob1.begin(), blob1.end());
    std::vector<uint8_t> b2(blob2.begin(), blob2.end());
    
    GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "poly1", b1);
    GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "poly2", b2);
    
    // Query: bbox that only overlaps poly1
    MBR query_box(10.0, 50.0, 10.6, 50.6);
    auto results = spatial_mgr_->searchIntersects("test_points", query_box);
    
    // Should only return poly1 (MBR check)
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].primary_key, "poly1");
}

// Test: Delete entity removes from spatial index
TEST_F(GeoIndexIntegrationTest, DeleteEntityRemovesFromIndex) {
    // Insert polygon
    json entity;
    entity["id"] = "poly1";
    entity["geometry"] = {
        {"type", "Polygon"},
        {"coordinates", json::array({
            json::array({
                json::array({10.0, 50.0}),
                json::array({11.0, 50.0}),
                json::array({11.0, 51.0}),
                json::array({10.0, 51.0}),
                json::array({10.0, 50.0})
            })
        })}
    };
    
    std::string blob_str = entity.dump();
    std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
    
    GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "poly1", blob);
    
    // Verify inserted
    MBR query_box(10.0, 50.0, 11.0, 51.0);
    auto results = spatial_mgr_->searchIntersects("test_points", query_box);
    ASSERT_EQ(results.size(), 1);
    
    // Delete entity
    GeoIndexHooks::onEntityDelete(*db_, spatial_mgr_.get(), "test_points", "poly1", blob);
    
    // Verify removed
    results = spatial_mgr_->searchIntersects("test_points", query_box);
    EXPECT_EQ(results.size(), 0);
}

// Test: Insert point geometry
TEST_F(GeoIndexIntegrationTest, InsertPointGeometry) {
    json entity;
    entity["id"] = "point1";
    entity["geometry"] = {
        {"type", "Point"},
        {"coordinates", json::array({10.5, 50.5})}
    };
    
    std::string blob_str = entity.dump();
    std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
    
    GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "point1", blob);
    
    // Search in bbox containing point
    MBR query_box(10.0, 50.0, 11.0, 51.0);
    auto results = spatial_mgr_->searchIntersects("test_points", query_box);
    
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].primary_key, "point1");
}

// Test: Hook handles missing geometry gracefully
TEST_F(GeoIndexIntegrationTest, HandlesMissingGeometry) {
    json entity;
    entity["id"] = "no_geom";
    entity["name"] = "Entity without geometry";
    
    std::string blob_str = entity.dump();
    std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
    
    // Should not crash or throw
    EXPECT_NO_THROW({
        GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "no_geom", blob);
    });
    
    // Index should remain empty
    MBR query_box(-180.0, -90.0, 180.0, 90.0);
    auto results = spatial_mgr_->searchIntersects("test_points", query_box);
    EXPECT_EQ(results.size(), 0);
}

// Test: Hook handles invalid JSON gracefully
TEST_F(GeoIndexIntegrationTest, HandlesInvalidJSON) {
    std::string invalid_json = "{invalid json";
    std::vector<uint8_t> blob(invalid_json.begin(), invalid_json.end());
    
    // Should not crash or throw
    EXPECT_NO_THROW({
        GeoIndexHooks::onEntityPut(*db_, spatial_mgr_.get(), "test_points", "invalid", blob);
    });
}

// Test: Hook works with null spatial manager (geo disabled)
TEST_F(GeoIndexIntegrationTest, HandlesNullSpatialManager) {
    json entity;
    entity["id"] = "test";
    entity["geometry"] = {
        {"type", "Point"},
        {"coordinates", json::array({10.5, 50.5})}
    };
    
    std::string blob_str = entity.dump();
    std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
    
    // Should not crash with null spatial manager
    EXPECT_NO_THROW({
        GeoIndexHooks::onEntityPut(*db_, nullptr, "test_points", "test", blob);
    });
}
