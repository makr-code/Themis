// Dispatch test for PROXIMITY Content+Geo hybrid

#include <gtest/gtest.h>
#include "query/aql_runner.h"
#include "query/query_engine.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"

using namespace themis;

class AQLProximityDispatchTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all("data/themis_aql_proximity_dispatch_test");
        RocksDBWrapper::Config cfg; cfg.db_path = "data/themis_aql_proximity_dispatch_test"; cfg.memtable_size_mb = 32; cfg.block_cache_size_mb = 32;
        db = std::make_unique<RocksDBWrapper>(cfg); ASSERT_TRUE(db->open());
        sec = std::make_unique<SecondaryIndexManager>(*db);
        engine = std::make_unique<QueryEngine>(*db, *sec);
        // Create fulltext index
        SecondaryIndexManager::FulltextConfig config; config.language="en"; config.stemming_enabled=true; config.stopwords_enabled=true;
        auto st = sec->createFulltextIndex("places", "description", config); ASSERT_TRUE(st.ok) << st.message;
        // Insert sample docs
        BaseEntity a("p1"); a.setField("description", "coffee shop berlin"); a.setField("location", nlohmann::json{{"type","Point"},{"coordinates", {13.45,52.55}}}); sec->put("places", a);
        BaseEntity b("p2"); b.setField("description", "coffee roastery berlin"); b.setField("location", nlohmann::json{{"type","Point"},{"coordinates", {13.46,52.551}}}); sec->put("places", b);
    }
    void TearDown() override { engine.reset(); sec.reset(); db.reset(); std::filesystem::remove_all("data/themis_aql_proximity_dispatch_test"); }
    std::unique_ptr<RocksDBWrapper> db; std::unique_ptr<SecondaryIndexManager> sec; std::unique_ptr<QueryEngine> engine;
};

TEST_F(AQLProximityDispatchTest, ExecuteProximityHybrid) {
    std::string aql = R"(
        FOR doc IN places
        FILTER FULLTEXT(doc.description, "coffee", 10)
        SORT PROXIMITY(doc.location, [13.45,52.55]) ASC
        LIMIT 5
        RETURN doc
    )";
    auto [status, jsonRes] = executeAql(aql, *engine);
    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_EQ(jsonRes["type"], "content_geo");
    ASSERT_TRUE(jsonRes.contains("results"));
    ASSERT_GE(jsonRes["results"].size(), 1);
    // Ensure geo_distance present
    auto first = jsonRes["results"][0];
    ASSERT_TRUE(first.contains("geo_distance"));
}
