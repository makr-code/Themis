#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

class SparseGeoIndexTest : public ::testing::Test {
protected:
    void SetUp() override {
	test_db_path_ = "./data/themis_sparse_geo_index_test";
        fs::remove_all(test_db_path_);
        
        themis::RocksDBWrapper::Config config;
        config.db_path = test_db_path_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        config.max_background_jobs = 2;
        config.compression_default = "lz4";
        config.compression_bottommost = "zstd";
        
        db_ = std::make_unique<themis::RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open());
        idx_ = std::make_unique<themis::SecondaryIndexManager>(*db_);
    }

    void TearDown() override {
        idx_.reset();
        db_.reset();
        fs::remove_all(test_db_path_);
    }

    std::string test_db_path_;
    std::unique_ptr<themis::RocksDBWrapper> db_;
    std::unique_ptr<themis::SecondaryIndexManager> idx_;
};

// ────────────────────────────────────────────────────────────────────────────
// Sparse-Index Tests
// ────────────────────────────────────────────────────────────────────────────

TEST_F(SparseGeoIndexTest, CreateAndDropSparseIndex) {
    auto st = idx_->createSparseIndex("users", "email");
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_TRUE(idx_->hasSparseIndex("users", "email"));

    auto st2 = idx_->dropSparseIndex("users", "email");
    ASSERT_TRUE(st2.ok) << st2.message;
    EXPECT_FALSE(idx_->hasSparseIndex("users", "email"));
}

TEST_F(SparseGeoIndexTest, SparseIndex_SkipsNullValues) {
    auto st = idx_->createSparseIndex("users", "email");
    ASSERT_TRUE(st.ok);

    // Entity mit email
    themis::BaseEntity e1("user1");
    e1.setField("name", "Alice");
    e1.setField("email", "alice@example.com");
    
    // Entity ohne email (NULL/fehlend)
    themis::BaseEntity e2("user2");
    e2.setField("name", "Bob");
    // email fehlt absichtlich

    // TODO: Implementierung muss in put() Sparse-Index-Logik hinzufügen
    // Für jetzt nur create/drop testen
}

TEST_F(SparseGeoIndexTest, SparseIndex_UniqueConstraint) {
    auto st = idx_->createSparseIndex("users", "email", true);
    ASSERT_TRUE(st.ok);
    EXPECT_TRUE(idx_->hasSparseIndex("users", "email"));
}

// ────────────────────────────────────────────────────────────────────────────
// Geo-Index Tests
// ────────────────────────────────────────────────────────────────────────────

TEST_F(SparseGeoIndexTest, CreateAndDropGeoIndex) {
    auto st = idx_->createGeoIndex("locations", "position");
    ASSERT_TRUE(st.ok) << st.message;
    EXPECT_TRUE(idx_->hasGeoIndex("locations", "position"));

    auto st2 = idx_->dropGeoIndex("locations", "position");
    ASSERT_TRUE(st2.ok) << st2.message;
    EXPECT_FALSE(idx_->hasGeoIndex("locations", "position"));
}

TEST_F(SparseGeoIndexTest, GeohashEncoding) {
    // Test Geohash encoding/decoding
    double lat = 52.52, lon = 13.405; // Berlin
    
    std::string geohash = themis::SecondaryIndexManager::encodeGeohash(lat, lon, 12);
    EXPECT_FALSE(geohash.empty());
    EXPECT_EQ(geohash.length(), 16u); // hex string of uint64_t

    auto [decoded_lat, decoded_lon] = themis::SecondaryIndexManager::decodeGeohash(geohash);
    
    // Should be close (precision depends on bit depth)
    EXPECT_NEAR(decoded_lat, lat, 0.1);
    EXPECT_NEAR(decoded_lon, lon, 0.1);
}

TEST_F(SparseGeoIndexTest, HaversineDistance) {
    // Berlin to Paris
    double berlin_lat = 52.52, berlin_lon = 13.405;
    double paris_lat = 48.8566, paris_lon = 2.3522;
    
    double dist = themis::SecondaryIndexManager::haversineDistance(
        berlin_lat, berlin_lon, paris_lat, paris_lon);
    
    // Approximate distance Berlin-Paris is ~877 km
    EXPECT_GT(dist, 800.0);
    EXPECT_LT(dist, 950.0);
}

TEST_F(SparseGeoIndexTest, GeoBox_ScanNonExistent) {
    auto st = idx_->createGeoIndex("locations", "position");
    ASSERT_TRUE(st.ok);

    // Scan empty index
    auto [st2, results] = idx_->scanGeoBox("locations", "position",
        50.0, 55.0,  // lat range
        10.0, 15.0,  // lon range
        100);
    
    ASSERT_TRUE(st2.ok) << st2.message;
    EXPECT_TRUE(results.empty());
}

TEST_F(SparseGeoIndexTest, GeoRadius_ScanNonExistent) {
    auto st = idx_->createGeoIndex("locations", "position");
    ASSERT_TRUE(st.ok);

    // Scan empty index
    auto [st2, results] = idx_->scanGeoRadius("locations", "position",
        52.52, 13.405,  // center (Berlin)
        100.0,           // radius km
        100);
    
    ASSERT_TRUE(st2.ok) << st2.message;
    EXPECT_TRUE(results.empty());
}

TEST_F(SparseGeoIndexTest, GeoIndex_NoIndexError) {
    // Scan without creating index
    auto [st, results] = idx_->scanGeoBox("locations", "position",
        50.0, 55.0, 10.0, 15.0, 100);
    
    EXPECT_FALSE(st.ok);
    EXPECT_TRUE(st.message.find("Kein Geo-Index") != std::string::npos);
}

// Integration-Test: Automatische Sparse-Index-Wartung bei put/erase
TEST_F(SparseGeoIndexTest, SparseIndex_AutoMaintenance) {
	// Sparse-Index erstellen
	auto st = idx_->createSparseIndex("Products", "discount", false);
	ASSERT_TRUE(st.ok);
	
	// Entities einfügen: einige mit discount, andere ohne (NULL/leer)
	themis::BaseEntity p1("p1");
	p1.setField("name", "Product A");
	p1.setField("discount", "10%");
	
	themis::BaseEntity p2("p2");
	p2.setField("name", "Product B");
	// Kein discount-Feld -> sollte nicht im Sparse-Index erscheinen
	
	themis::BaseEntity p3("p3");
	p3.setField("name", "Product C");
	p3.setField("discount", "");  // Leerer Wert -> sollte nicht im Sparse-Index erscheinen
	
	themis::BaseEntity p4("p4");
	p4.setField("name", "Product D");
	p4.setField("discount", "20%");
	
	// Entities speichern
	ASSERT_TRUE(idx_->put("Products", p1).ok);
	ASSERT_TRUE(idx_->put("Products", p2).ok);
	ASSERT_TRUE(idx_->put("Products", p3).ok);
	ASSERT_TRUE(idx_->put("Products", p4).ok);
	
	// Scan: Nur p1 und p4 sollten im Index sein (mit discount-Werten)
	auto [st1, pks1] = idx_->scanKeysEqual("Products", "discount", "10%");
	ASSERT_TRUE(st1.ok);
	EXPECT_EQ(pks1.size(), 1);
	EXPECT_EQ(pks1[0], "p1");
	
	auto [st2, pks2] = idx_->scanKeysEqual("Products", "discount", "20%");
	ASSERT_TRUE(st2.ok);
	EXPECT_EQ(pks2.size(), 1);
	EXPECT_EQ(pks2[0], "p4");
	
	// Leerer Wert sollte nicht gefunden werden
	auto [st3, pks3] = idx_->scanKeysEqual("Products", "discount", "");
	ASSERT_TRUE(st3.ok);
	EXPECT_TRUE(pks3.empty());
	
	// p1 löschen -> Index-Eintrag sollte verschwinden
	ASSERT_TRUE(idx_->erase("Products", "p1").ok);
	auto [st4, pks4] = idx_->scanKeysEqual("Products", "discount", "10%");
	ASSERT_TRUE(st4.ok);
	EXPECT_TRUE(pks4.empty());
	
	// p4 sollte noch da sein
	auto [st5, pks5] = idx_->scanKeysEqual("Products", "discount", "20%");
	ASSERT_TRUE(st5.ok);
	EXPECT_EQ(pks5.size(), 1);
	EXPECT_EQ(pks5[0], "p4");
}

// Integration-Test: Automatische Geo-Index-Wartung bei put/erase
TEST_F(SparseGeoIndexTest, GeoIndex_AutoMaintenance) {
	// Geo-Index erstellen
	auto st = idx_->createGeoIndex("Locations", "position");
	ASSERT_TRUE(st.ok);
	
	// Locations einfügen (Berlin, Paris, London)
	themis::BaseEntity berlin("berlin");
	berlin.setField("name", "Berlin");
	berlin.setField("position_lat", "52.52");
	berlin.setField("position_lon", "13.405");
	
	themis::BaseEntity paris("paris");
	paris.setField("name", "Paris");
	paris.setField("position_lat", "48.8566");
	paris.setField("position_lon", "2.3522");
	
	themis::BaseEntity london("london");
	london.setField("name", "London");
	london.setField("position_lat", "51.5074");
	london.setField("position_lon", "-0.1278");
	
	themis::BaseEntity tokyo("tokyo");
	tokyo.setField("name", "Tokyo");
	tokyo.setField("position_lat", "35.6762");
	tokyo.setField("position_lon", "139.6503");
	
	// Speichern
	ASSERT_TRUE(idx_->put("Locations", berlin).ok);
	ASSERT_TRUE(idx_->put("Locations", paris).ok);
	ASSERT_TRUE(idx_->put("Locations", london).ok);
	ASSERT_TRUE(idx_->put("Locations", tokyo).ok);
	
	// Bounding Box: Europa (Lat: 40-60, Lon: -10-20)
	auto [st1, pks1] = idx_->scanGeoBox("Locations", "position", 40.0, 60.0, -10.0, 20.0);
	ASSERT_TRUE(st1.ok);
	EXPECT_EQ(pks1.size(), 3);  // Berlin, Paris, London
	
	// Radius-Suche: 500km um Berlin
	auto [st2, pks2] = idx_->scanGeoRadius("Locations", "position", 52.52, 13.405, 500.0);
	ASSERT_TRUE(st2.ok);
	EXPECT_GE(pks2.size(), 1);  // Mindestens Berlin selbst
	// Paris ist ~877km entfernt, London ~930km -> sollten nicht dabei sein
	EXPECT_LE(pks2.size(), 1);
	
	// Tokyo löschen
	ASSERT_TRUE(idx_->erase("Locations", "tokyo").ok);
	
	// Bounding Box weltweit sollte Tokyo nicht mehr enthalten
	auto [st3, pks3] = idx_->scanGeoBox("Locations", "position", -90.0, 90.0, -180.0, 180.0);
	ASSERT_TRUE(st3.ok);
	EXPECT_EQ(pks3.size(), 3);  // Berlin, Paris, London (ohne Tokyo)
	
	// Verify Tokyo ist wirklich weg
	bool found_tokyo = false;
	for (const auto& pk : pks3) {
		if (pk == "tokyo") found_tokyo = true;
	}
	EXPECT_FALSE(found_tokyo);
}

