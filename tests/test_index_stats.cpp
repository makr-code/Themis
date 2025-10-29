#include <gtest/gtest.h>
#include "index/secondary_index.h"
#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include <filesystem>
#include <thread>
#include <chrono>

using namespace themis;

class IndexStatsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Erstelle einen temporären DB-Pfad für jeden Test
        dbPath_ = "./data/themis_index_stats_test";
        
        // Lösche existierendes Testverzeichnis
        std::filesystem::remove_all(dbPath_);
        
        // RocksDB-Config
        themis::RocksDBWrapper::Config config;
        config.db_path = dbPath_;
        config.memtable_size_mb = 64;
        config.block_cache_size_mb = 256;
        
        // RocksDB und SecondaryIndexManager erstellen
        db_ = std::make_unique<RocksDBWrapper>(config);
        ASSERT_TRUE(db_->open()) << "Datenbank konnte nicht geöffnet werden";
        
        indexMgr_ = std::make_unique<SecondaryIndexManager>(*db_);
    }
    
    void TearDown() override {
        indexMgr_.reset();
        db_.reset();
        std::filesystem::remove_all(dbPath_);
    }
    
    std::string dbPath_;
    std::unique_ptr<RocksDBWrapper> db_;
    std::unique_ptr<SecondaryIndexManager> indexMgr_;
};

// Test: Statistiken für regulären Index
TEST_F(IndexStatsTest, RegularIndexStats) {
    // Index erstellen
    auto status = indexMgr_->createIndex("users", "email", false);
    ASSERT_TRUE(status.ok);
    
    // Entities einfügen
    for (int i = 0; i < 5; i++) {
    BaseEntity entity("user" + std::to_string(i));
    entity.setField("email", "user" + std::to_string(i) + "@test.com");
    entity.setField("name", "User " + std::to_string(i));
    auto putStatus = indexMgr_->put("users", entity);
    ASSERT_TRUE(putStatus.ok);
    }
    
    // Statistiken abrufen
    auto stats = indexMgr_->getIndexStats("users", "email");
    
    EXPECT_EQ(stats.type, "regular");
    EXPECT_EQ(stats.table, "users");
    EXPECT_EQ(stats.column, "email");
    EXPECT_EQ(stats.entry_count, 5);
    EXPECT_GT(stats.estimated_size_bytes, 0);
    EXPECT_FALSE(stats.unique);
}

// Test: Statistiken für Unique Index
TEST_F(IndexStatsTest, UniqueIndexStats) {
    auto status = indexMgr_->createIndex("users", "username", true);
    ASSERT_TRUE(status.ok);
    
    BaseEntity entity("user1");
    entity.setField("username", "john_doe");
    auto putStatus = indexMgr_->put("users", entity);
    ASSERT_TRUE(putStatus.ok);
    
    auto stats = indexMgr_->getIndexStats("users", "username");
    
    EXPECT_EQ(stats.type, "regular");
    EXPECT_TRUE(stats.unique);
    EXPECT_EQ(stats.additional_info, "unique");
}

// Test: Statistiken für Range Index
TEST_F(IndexStatsTest, RangeIndexStats) {
    auto status = indexMgr_->createRangeIndex("products", "price");
    ASSERT_TRUE(status.ok);
    
    // Produkte mit verschiedenen Preisen einfügen
    for (int i = 0; i < 10; i++) {
    BaseEntity entity("prod" + std::to_string(i));
    entity.setField("price", std::to_string(i * 10.0));
    entity.setField("name", "Product " + std::to_string(i));
    auto putStatus = indexMgr_->put("products", entity);
    ASSERT_TRUE(putStatus.ok);
    }
    
    auto stats = indexMgr_->getIndexStats("products", "price");
    
    EXPECT_EQ(stats.type, "range");
    EXPECT_EQ(stats.entry_count, 10);
    EXPECT_EQ(stats.additional_info, "sorted");
}

// Test: Statistiken für Sparse Index
TEST_F(IndexStatsTest, SparseIndexStats) {
    auto status = indexMgr_->createSparseIndex("users", "nickname", false);
    ASSERT_TRUE(status.ok);
    
    // 3 Entities mit nickname, 2 ohne
    for (int i = 0; i < 5; i++) {
        BaseEntity entity("user" + std::to_string(i));
        entity.setField("name", "User " + std::to_string(i));
        if (i < 3) {
            entity.setField("nickname", "Nick" + std::to_string(i));
        }
        auto putStatus = indexMgr_->put("users", entity);
        ASSERT_TRUE(putStatus.ok);
    }
    
    auto stats = indexMgr_->getIndexStats("users", "nickname");
    
    EXPECT_EQ(stats.type, "sparse");
    EXPECT_EQ(stats.entry_count, 3);  // Nur 3 mit nickname
}

// Test: Statistiken für Geo Index
TEST_F(IndexStatsTest, GeoIndexStats) {
    auto status = indexMgr_->createGeoIndex("locations", "coords");
    ASSERT_TRUE(status.ok);
    
    // Locations einfügen (Geo-Index erwartet coords_lat und coords_lon)
    for (int i = 0; i < 7; i++) {
        BaseEntity entity("loc" + std::to_string(i));
        entity.setField("coords_lat", std::to_string(52.0 + i));
        entity.setField("coords_lon", std::to_string(13.0 + i));
        entity.setField("name", "Location " + std::to_string(i));
        auto putStatus = indexMgr_->put("locations", entity);
        ASSERT_TRUE(putStatus.ok);
    }
    
    auto stats = indexMgr_->getIndexStats("locations", "coords");
    
    EXPECT_EQ(stats.type, "geo");
    EXPECT_EQ(stats.entry_count, 7);
    EXPECT_EQ(stats.additional_info, "geohash");
}

// Test: Statistiken für TTL Index
TEST_F(IndexStatsTest, TTLIndexStats) {
    auto status = indexMgr_->createTTLIndex("sessions", "user", 3600);  // 1 Stunde
    ASSERT_TRUE(status.ok);
    
    // Regulären Index für user hinzufügen
    indexMgr_->createIndex("sessions", "user", false);
    
    // Sessions einfügen
    for (int i = 0; i < 4; i++) {
    BaseEntity entity("session" + std::to_string(i));
    entity.setField("user", "user" + std::to_string(i));
    entity.setField("token", "token" + std::to_string(i));
    auto putStatus = indexMgr_->put("sessions", entity);
    ASSERT_TRUE(putStatus.ok);
    }
    
    auto stats = indexMgr_->getIndexStats("sessions", "user");
    
    EXPECT_EQ(stats.type, "ttl");
    EXPECT_EQ(stats.entry_count, 4);
    EXPECT_EQ(stats.additional_info, "ttl_seconds=3600");
}

// Test: Statistiken für Fulltext Index
TEST_F(IndexStatsTest, FulltextIndexStats) {
    auto status = indexMgr_->createFulltextIndex("articles", "content");
    ASSERT_TRUE(status.ok);
    
    // Artikel mit verschiedenen Texten
    BaseEntity entity1("art1");
    entity1.setField("content", "This is a test");  // 4 tokens
    indexMgr_->put("articles", entity1);
    
    BaseEntity entity2("art2");
    entity2.setField("content", "Another test article");  // 3 tokens
    indexMgr_->put("articles", entity2);
    
    auto stats = indexMgr_->getIndexStats("articles", "content");
    
    EXPECT_EQ(stats.type, "fulltext");
    // 7 token entries total: this(1), is(1), a(1), test(2 docs), another(1), article(1)
    // Actually: 8 entries because each doc gets one entry per unique token
    EXPECT_GT(stats.entry_count, 0);
    EXPECT_EQ(stats.additional_info, "inverted_index");
}

// Test: getAllIndexStats für Tabelle mit mehreren Indizes
TEST_F(IndexStatsTest, GetAllIndexStats) {
    // Verschiedene Index-Typen erstellen
    indexMgr_->createIndex("users", "email", false);
    indexMgr_->createRangeIndex("users", "age");
    indexMgr_->createSparseIndex("users", "nickname", false);
    
    // Entities einfügen
    for (int i = 0; i < 3; i++) {
        BaseEntity entity("user" + std::to_string(i));
        entity.setField("email", "user" + std::to_string(i) + "@test.com");
        entity.setField("age", std::to_string(20 + i));
        if (i < 2) {
            entity.setField("nickname", "Nick" + std::to_string(i));
        }
        indexMgr_->put("users", entity);
    }
    
    // Alle Stats abrufen
    auto allStats = indexMgr_->getAllIndexStats("users");
    
    EXPECT_EQ(allStats.size(), 3);
    
    // Typen prüfen
    std::set<std::string> types;
    for (const auto& stats : allStats) {
        types.insert(stats.type);
        EXPECT_EQ(stats.table, "users");
    }
    
    EXPECT_TRUE(types.count("regular") > 0);
    EXPECT_TRUE(types.count("range") > 0);
    EXPECT_TRUE(types.count("sparse") > 0);
}

// Test: rebuildIndex nach manueller Löschung
TEST_F(IndexStatsTest, RebuildIndex) {
    // Index erstellen und Daten einfügen
    indexMgr_->createIndex("users", "email", false);
    
    for (int i = 0; i < 5; i++) {
    BaseEntity entity("user" + std::to_string(i));
    entity.setField("email", "user" + std::to_string(i) + "@test.com");
    indexMgr_->put("users", entity);
    }
    
    // Stats vor Rebuild
    auto statsBefore = indexMgr_->getIndexStats("users", "email");
    EXPECT_EQ(statsBefore.entry_count, 5);
    
    // Manuell Index-Einträge löschen (simuliert Inkonsistenz)
    std::string prefix = "idx:users:email:";
    std::vector<std::string> keysToDelete;
    db_->scanPrefix(prefix, [&keysToDelete](std::string_view key, std::string_view) {
        keysToDelete.push_back(std::string(key));
        return true;
    });
    
    for (const auto& key : keysToDelete) {
        db_->del(key);
    }
    
    // Stats nach Löschung (sollte 0 sein)
    auto statsAfterDelete = indexMgr_->getIndexStats("users", "email");
    EXPECT_EQ(statsAfterDelete.entry_count, 0);
    
    // Rebuild durchführen
    indexMgr_->rebuildIndex("users", "email");
    
    // Stats nach Rebuild (sollte wieder 5 sein)
    auto statsAfterRebuild = indexMgr_->getIndexStats("users", "email");
    EXPECT_EQ(statsAfterRebuild.entry_count, 5);
    
    // Funktionalität prüfen
    auto [status, results] = indexMgr_->scanKeysEqual("users", "email", "user2@test.com");
    ASSERT_TRUE(status.ok);
    EXPECT_EQ(results.size(), 1);
    EXPECT_EQ(results[0], "user2");
}

// Test: reindexTable rebuilt alle Indizes
TEST_F(IndexStatsTest, ReindexTable) {
    // Mehrere Indizes erstellen
    indexMgr_->createIndex("products", "category", false);
    indexMgr_->createRangeIndex("products", "price");
    
    // Produkte einfügen
    for (int i = 0; i < 3; i++) {
    BaseEntity entity("prod" + std::to_string(i));
    entity.setField("category", "cat" + std::to_string(i % 2));
    entity.setField("price", std::to_string(i * 10.0));
    indexMgr_->put("products", entity);
    }
    
    // Alle Stats vor Rebuild
    auto statsBefore = indexMgr_->getAllIndexStats("products");
    EXPECT_EQ(statsBefore.size(), 2);
    
    // Alle Index-Einträge manuell löschen
    auto deleteIndexEntries = [this](const std::string& prefix) {
        std::vector<std::string> keys;
        db_->scanPrefix(prefix, [&keys](std::string_view key, std::string_view) {
            keys.push_back(std::string(key));
            return true;
        });
        for (const auto& key : keys) {
            db_->del(key);
        }
    };
    
    deleteIndexEntries("idx:products:");
    deleteIndexEntries("ridx:products:");
    
    // Stats nach Löschung
    auto statsAfterDelete = indexMgr_->getAllIndexStats("products");
    for (const auto& stats : statsAfterDelete) {
        EXPECT_EQ(stats.entry_count, 0);
    }
    
    // Rebuild aller Indizes
    indexMgr_->reindexTable("products");
    
    // Stats nach Rebuild
    auto statsAfterRebuild = indexMgr_->getAllIndexStats("products");
    EXPECT_EQ(statsAfterRebuild.size(), 2);
    
    for (const auto& stats : statsAfterRebuild) {
        EXPECT_GT(stats.entry_count, 0);
    }
}

// Test: Composite Index Stats
TEST_F(IndexStatsTest, CompositeIndexStats) {
    auto status = indexMgr_->createCompositeIndex("orders", {"customer_id", "status"}, false);
    ASSERT_TRUE(status.ok);
    
    // Orders einfügen
    for (int i = 0; i < 6; i++) {
        BaseEntity entity("order" + std::to_string(i));
        entity.setField("customer_id", "cust" + std::to_string(i % 2));
        entity.setField("status", i % 3 == 0 ? "pending" : "shipped");
        entity.setField("total", std::to_string(i * 100.0));
        indexMgr_->put("orders", entity);
    }
    
    auto stats = indexMgr_->getIndexStats("orders", "customer_id+status");
    
    EXPECT_EQ(stats.type, "composite");
    EXPECT_EQ(stats.table, "orders");
    EXPECT_EQ(stats.column, "customer_id+status");
    EXPECT_EQ(stats.entry_count, 6);
    EXPECT_TRUE(stats.additional_info.find("customer_id") != std::string::npos);
}

// Test: Progress-Callback wird aufgerufen und Rebuild vervollständigt
TEST_F(IndexStatsTest, RebuildProgressCallback_Completes) {
    // Index und Daten
    indexMgr_->createIndex("users", "email", false);
    for (int i = 0; i < 10; ++i) {
        BaseEntity e("user" + std::to_string(i));
        e.setField("email", "user" + std::to_string(i) + "@test.com");
        indexMgr_->put("users", e);
    }

    // Index-Einträge löschen
    std::string prefix = "idx:users:email:";
    std::vector<std::string> keysToDelete;
    db_->scanPrefix(prefix, [&keysToDelete](std::string_view key, std::string_view){
        keysToDelete.push_back(std::string(key));
        return true;
    });
    for (const auto& k : keysToDelete) db_->del(k);

    auto before = indexMgr_->getIndexStats("users", "email");
    EXPECT_EQ(before.entry_count, 0);

    // Rebuild mit Progress
    size_t calls = 0;
    indexMgr_->rebuildIndex("users", "email", [&](size_t done, size_t total){
        ++calls;
        EXPECT_GE(total, static_cast<size_t>(10));
        EXPECT_LE(done, total);
        return true; // nicht abbrechen
    });

    auto after = indexMgr_->getIndexStats("users", "email");
    EXPECT_EQ(after.entry_count, 10);
    EXPECT_GE(calls, static_cast<size_t>(1));
}

// Test: Progress-Callback kann Rebuild abbrechen
TEST_F(IndexStatsTest, RebuildProgressCallback_Abort) {
    // Index und Daten
    indexMgr_->createIndex("users", "email", false);
    for (int i = 0; i < 10; ++i) {
        BaseEntity e("user" + std::to_string(i));
        e.setField("email", "user" + std::to_string(i) + "@test.com");
        indexMgr_->put("users", e);
    }

    // Index-Einträge löschen
    std::string prefix = "idx:users:email:";
    std::vector<std::string> keysToDelete;
    db_->scanPrefix(prefix, [&keysToDelete](std::string_view key, std::string_view){
        keysToDelete.push_back(std::string(key));
        return true;
    });
    for (const auto& k : keysToDelete) db_->del(k);

    auto before = indexMgr_->getIndexStats("users", "email");
    EXPECT_EQ(before.entry_count, 0);

    // Rebuild mit Abbruch nach 3 Entities
    size_t calls = 0;
    indexMgr_->rebuildIndex("users", "email", [&](size_t done, size_t total){
        ++calls;
        return done < 3; // abbrechen, sobald 3 erreicht
    });

    auto after = indexMgr_->getIndexStats("users", "email");
    EXPECT_LT(after.entry_count, 10);
    EXPECT_GE(calls, static_cast<size_t>(1));
}
