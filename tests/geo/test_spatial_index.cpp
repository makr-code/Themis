#include <gtest/gtest.h>
#include "index/spatial_index.h"
#include "storage/storage_engine.h"
#include <memory>
#include <cmath>

using namespace themis;
using namespace themis::index;
using namespace themis::geo;

class SpatialIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
        storage_ = std::make_shared<StorageEngine>("test_spatial_index.db");
        spatial_mgr_ = std::make_unique<SpatialIndexManager>(storage_);
    }
    
    void TearDown() override {
        spatial_mgr_.reset();
        storage_.reset();
        std::remove("test_spatial_index.db");
    }
    
    bool approxEqual(double a, double b, double epsilon = 1e-6) {
        return std::fabs(a - b) < epsilon;
    }
    
    std::shared_ptr<StorageEngine> storage_;
    std::unique_ptr<SpatialIndexManager> spatial_mgr_;
};

// Test: Create spatial index for table
TEST_F(SpatialIndexTest, CreateIndex) {
    RTreeConfig config;
    config.total_bounds = MBR(-180.0, -90.0, 180.0, 90.0);
    
    auto status = spatial_mgr_->createSpatialIndex("cities", "geometry", config);
    EXPECT_TRUE(status.ok());
    
    EXPECT_TRUE(spatial_mgr_->hasSpatialIndex("cities"));
    EXPECT_FALSE(spatial_mgr_->hasSpatialIndex("nonexistent"));
}

// Test: Insert and search single point
TEST_F(SpatialIndexTest, InsertAndSearchPoint) {
    // Create index
    spatial_mgr_->createSpatialIndex("cities");
    
    // Insert Berlin (13.4, 52.5)
    GeoSidecar berlin_sidecar;
    berlin_sidecar.mbr = MBR(13.4, 52.5, 13.4, 52.5);
    berlin_sidecar.centroid = Coordinate(13.4, 52.5);
    
    auto status = spatial_mgr_->insert("cities", "cities/berlin", berlin_sidecar);
    EXPECT_TRUE(status.ok());
    
    // Search: query box around Berlin
    MBR query_box(13.0, 52.0, 14.0, 53.0);
    auto results = spatial_mgr_->searchIntersects("cities", query_box);
    
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].primary_key, "cities/berlin");
    EXPECT_TRUE(approxEqual(results[0].mbr.minx, 13.4));
}

// Test: Multiple points in same region
TEST_F(SpatialIndexTest, MultiplePoints) {
    spatial_mgr_->createSpatialIndex("locations");
    
    // Insert multiple German cities
    struct City {
        std::string id;
        double lon, lat;
    };
    
    std::vector<City> cities = {
        {"berlin", 13.4, 52.5},
        {"munich", 11.6, 48.1},
        {"hamburg", 10.0, 53.6},
        {"cologne", 6.96, 50.94}
    };
    
    for (const auto& city : cities) {
        GeoSidecar sidecar;
        sidecar.mbr = MBR(city.lon, city.lat, city.lon, city.lat);
        sidecar.centroid = Coordinate(city.lon, city.lat);
        
        spatial_mgr_->insert("locations", "locations/" + city.id, sidecar);
    }
    
    // Search: Germany bounding box
    MBR germany(6.0, 47.0, 15.0, 55.0);
    auto results = spatial_mgr_->searchIntersects("locations", germany);
    
    EXPECT_EQ(results.size(), 4);
}

// Test: Search within (strict containment)
TEST_F(SpatialIndexTest, SearchWithin) {
    spatial_mgr_->createSpatialIndex("regions");
    
    // Insert regions (as bounding boxes)
    GeoSidecar bavaria;  // Large region
    bavaria.mbr = MBR(10.0, 47.0, 13.5, 50.5);
    bavaria.centroid = bavaria.mbr.center();
    spatial_mgr_->insert("regions", "regions/bavaria", bavaria);
    
    GeoSidecar small_area;  // Small region fully inside Bavaria
    small_area.mbr = MBR(11.5, 48.0, 11.7, 48.2);
    small_area.centroid = small_area.mbr.center();
    spatial_mgr_->insert("regions", "regions/munich_area", small_area);
    
    // Search: Find regions within Bavaria bounds
    MBR query = bavaria.mbr;
    auto results = spatial_mgr_->searchWithin("regions", query);
    
    // Both should intersect, but only small_area is fully within
    EXPECT_EQ(results.size(), 2);  // bavaria itself + munich_area
}

// Test: Search contains point
TEST_F(SpatialIndexTest, SearchContainsPoint) {
    spatial_mgr_->createSpatialIndex("areas");
    
    // Insert a region
    GeoSidecar region;
    region.mbr = MBR(10.0, 50.0, 12.0, 52.0);
    region.centroid = region.mbr.center();
    spatial_mgr_->insert("areas", "areas/region1", region);
    
    // Search: point inside region
    auto results_inside = spatial_mgr_->searchContains("areas", 11.0, 51.0);
    EXPECT_EQ(results_inside.size(), 1);
    
    // Search: point outside region
    auto results_outside = spatial_mgr_->searchContains("areas", 15.0, 51.0);
    EXPECT_EQ(results_outside.size(), 0);
}

// Test: Search nearby (distance-based)
TEST_F(SpatialIndexTest, SearchNearby) {
    spatial_mgr_->createSpatialIndex("pois");
    
    // Insert points of interest around Berlin (13.4, 52.5)
    struct POI {
        std::string id;
        double lon, lat;
    };
    
    std::vector<POI> pois = {
        {"brandenburger_tor", 13.377, 52.516},   // ~1.5 km from center
        {"alexanderplatz", 13.413, 52.521},       // ~1 km
        {"tempelhofer_feld", 13.405, 52.473},     // ~3 km
        {"potsdam", 13.064, 52.399}               // ~30 km
    };
    
    for (const auto& poi : pois) {
        GeoSidecar sidecar;
        sidecar.mbr = MBR(poi.lon, poi.lat, poi.lon, poi.lat);
        sidecar.centroid = Coordinate(poi.lon, poi.lat);
        
        spatial_mgr_->insert("pois", "pois/" + poi.id, sidecar);
    }
    
    // Search: POIs within 5km of Berlin center
    auto results = spatial_mgr_->searchNearby("pois", 13.4, 52.5, 5000.0);
    
    // Should find 3 (not Potsdam which is ~30km away)
    EXPECT_EQ(results.size(), 3);
    
    // Results should be sorted by distance
    EXPECT_LT(results[0].distance, results[1].distance);
    EXPECT_LT(results[1].distance, results[2].distance);
}

// Test: Multi-table support (different models)
TEST_F(SpatialIndexTest, MultiTableSupport) {
    // Create indexes for different models
    spatial_mgr_->createSpatialIndex("cities");        // Relational
    spatial_mgr_->createSpatialIndex("locations");     // Graph nodes
    spatial_mgr_->createSpatialIndex("images");        // Vector entities
    spatial_mgr_->createSpatialIndex("documents");     // Content
    
    EXPECT_TRUE(spatial_mgr_->hasSpatialIndex("cities"));
    EXPECT_TRUE(spatial_mgr_->hasSpatialIndex("locations"));
    EXPECT_TRUE(spatial_mgr_->hasSpatialIndex("images"));
    EXPECT_TRUE(spatial_mgr_->hasSpatialIndex("documents"));
    
    // Insert into different tables
    GeoSidecar sidecar;
    sidecar.mbr = MBR(13.4, 52.5, 13.4, 52.5);
    sidecar.centroid = Coordinate(13.4, 52.5);
    
    spatial_mgr_->insert("cities", "cities/berlin", sidecar);
    spatial_mgr_->insert("locations", "locations/loc1", sidecar);
    spatial_mgr_->insert("images", "images/img1", sidecar);
    spatial_mgr_->insert("documents", "documents/doc1", sidecar);
    
    // Search each table independently
    MBR query(13.0, 52.0, 14.0, 53.0);
    
    auto cities_results = spatial_mgr_->searchIntersects("cities", query);
    auto locations_results = spatial_mgr_->searchIntersects("locations", query);
    auto images_results = spatial_mgr_->searchIntersects("images", query);
    auto docs_results = spatial_mgr_->searchIntersects("documents", query);
    
    EXPECT_EQ(cities_results.size(), 1);
    EXPECT_EQ(locations_results.size(), 1);
    EXPECT_EQ(images_results.size(), 1);
    EXPECT_EQ(docs_results.size(), 1);
    
    // Verify isolation (cities query doesn't return locations)
    EXPECT_EQ(cities_results[0].primary_key, "cities/berlin");
    EXPECT_EQ(locations_results[0].primary_key, "locations/loc1");
}

// Test: Update location
TEST_F(SpatialIndexTest, UpdateLocation) {
    spatial_mgr_->createSpatialIndex("vehicles");
    
    // Initial location
    GeoSidecar old_sidecar;
    old_sidecar.mbr = MBR(13.4, 52.5, 13.4, 52.5);
    old_sidecar.centroid = Coordinate(13.4, 52.5);
    spatial_mgr_->insert("vehicles", "vehicles/car1", old_sidecar);
    
    // Update to new location
    GeoSidecar new_sidecar;
    new_sidecar.mbr = MBR(13.5, 52.6, 13.5, 52.6);
    new_sidecar.centroid = Coordinate(13.5, 52.6);
    spatial_mgr_->update("vehicles", "vehicles/car1", old_sidecar, new_sidecar);
    
    // Search old location
    MBR old_query(13.35, 52.45, 13.45, 52.55);
    auto old_results = spatial_mgr_->searchIntersects("vehicles", old_query);
    EXPECT_EQ(old_results.size(), 0);  // Should not find in old location
    
    // Search new location
    MBR new_query(13.45, 52.55, 13.55, 52.65);
    auto new_results = spatial_mgr_->searchIntersects("vehicles", new_query);
    EXPECT_EQ(new_results.size(), 1);  // Should find in new location
}

// Test: Remove entity
TEST_F(SpatialIndexTest, RemoveEntity) {
    spatial_mgr_->createSpatialIndex("temp");
    
    GeoSidecar sidecar;
    sidecar.mbr = MBR(13.4, 52.5, 13.4, 52.5);
    sidecar.centroid = Coordinate(13.4, 52.5);
    
    spatial_mgr_->insert("temp", "temp/item1", sidecar);
    
    // Verify exists
    MBR query(13.0, 52.0, 14.0, 53.0);
    auto before = spatial_mgr_->searchIntersects("temp", query);
    EXPECT_EQ(before.size(), 1);
    
    // Remove
    spatial_mgr_->remove("temp", "temp/item1", sidecar);
    
    // Verify removed
    auto after = spatial_mgr_->searchIntersects("temp", query);
    EXPECT_EQ(after.size(), 0);
}

// Test: Index statistics
TEST_F(SpatialIndexTest, IndexStats) {
    spatial_mgr_->createSpatialIndex("stats_test");
    
    // Insert multiple entities
    for (int i = 0; i < 10; ++i) {
        GeoSidecar sidecar;
        sidecar.mbr = MBR(13.0 + i * 0.1, 52.0 + i * 0.1, 13.0 + i * 0.1, 52.0 + i * 0.1);
        sidecar.centroid = sidecar.mbr.center();
        
        spatial_mgr_->insert("stats_test", "stats_test/item" + std::to_string(i), sidecar);
    }
    
    auto stats = spatial_mgr_->getStats("stats_test");
    
    EXPECT_EQ(stats.entry_count, 10);
    EXPECT_GT(stats.morton_buckets, 0);
    EXPECT_TRUE(approxEqual(stats.total_bounds.minx, -180.0));
    EXPECT_TRUE(approxEqual(stats.total_bounds.maxx, 180.0));
}

// Test: Morton encoder
TEST_F(SpatialIndexTest, MortonEncoder) {
    MBR bounds(-180.0, -90.0, 180.0, 90.0);
    
    // Encode and decode
    uint64_t code = MortonEncoder::encode2D(13.4, 52.5, bounds);
    auto [x, y] = MortonEncoder::decode2D(code, bounds);
    
    // Should be approximately equal (some precision loss)
    EXPECT_TRUE(approxEqual(x, 13.4, 0.01));
    EXPECT_TRUE(approxEqual(y, 52.5, 0.01));
    
    // Spatial locality: nearby points should have similar codes
    uint64_t code1 = MortonEncoder::encode2D(13.4, 52.5, bounds);
    uint64_t code2 = MortonEncoder::encode2D(13.5, 52.6, bounds);  // Nearby
    uint64_t code3 = MortonEncoder::encode2D(100.0, 20.0, bounds);  // Far away
    
    // Codes for nearby points should be closer than distant points
    uint64_t diff_nearby = std::abs(static_cast<int64_t>(code1 - code2));
    uint64_t diff_far = std::abs(static_cast<int64_t>(code1 - code3));
    
    EXPECT_LT(diff_nearby, diff_far);
}

// Test: Drop index
TEST_F(SpatialIndexTest, DropIndex) {
    spatial_mgr_->createSpatialIndex("drop_test");
    
    GeoSidecar sidecar;
    sidecar.mbr = MBR(13.4, 52.5, 13.4, 52.5);
    sidecar.centroid = Coordinate(13.4, 52.5);
    spatial_mgr_->insert("drop_test", "drop_test/item1", sidecar);
    
    EXPECT_TRUE(spatial_mgr_->hasSpatialIndex("drop_test"));
    
    // Drop index
    spatial_mgr_->dropSpatialIndex("drop_test");
    
    EXPECT_FALSE(spatial_mgr_->hasSpatialIndex("drop_test"));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
