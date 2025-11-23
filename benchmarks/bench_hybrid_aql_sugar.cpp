// Benchmark: AQL Syntax Sugar vs Direct C++ API for Hybrid Queries
// Vergleicht Latenz und Durchsatz zwischen drei Zugriffsmethoden:
// 1. AQL Syntax Sugar (SIMILARITY/PROXIMITY/SHORTEST_PATH)
// 2. Direkte C++ API (executeVectorGeoQuery etc.)
// 3. Manueller Code (ohne Query Engine Abstraktion)

#include "query/query_engine.h"
#include "query/aql_parser.h"
#include "query/aql_translator.h"
#include "query/aql_runner.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/vector_index.h"
#include "index/spatial_index.h"
#include "index/graph_index.h"
#include "storage/base_entity.h"
#include "utils/logger.h"
#include "utils/tracing.h"
#include "utils/geo/ewkb.h"

#include <benchmark/benchmark.h>
#include <nlohmann/json.hpp>
#include <random>
#include <cmath>

using namespace themis;

// ============================================================================
// Test Data Setup
// ============================================================================

static RocksDBWrapper* g_db = nullptr;
static SecondaryIndexManager* g_secIdx = nullptr;
static VectorIndexManager* g_vectorIdx = nullptr;
static SpatialIndexManager* g_spatialIdx = nullptr;
static GraphIndexManager* g_graphIdx = nullptr;
static QueryEngine* g_engine = nullptr;

constexpr size_t NUM_HOTELS = 1000;
constexpr size_t VECTOR_DIM = 128;

static void SetupTestData() {
    // RocksDBWrapper benötigt Config
    RocksDBWrapper::Config cfg; cfg.db_path = "bench_hybrid_aql_tmp.db"; 
    g_db = new RocksDBWrapper(cfg);
    g_db->open();

    g_secIdx = new SecondaryIndexManager(*g_db);
    g_vectorIdx = new VectorIndexManager(*g_db);
    g_spatialIdx = new SpatialIndexManager(*g_db);
    g_graphIdx = new GraphIndexManager(*g_db);

    g_engine = new QueryEngine(*g_db, *g_secIdx, *g_graphIdx, g_vectorIdx, g_spatialIdx);

    // Sekundär-/Range-/Composite-Indizes
    g_secIdx->createIndex("hotels", "city");
    g_secIdx->createRangeIndex("hotels", "stars");
    g_secIdx->createCompositeIndex("hotels", {"city", "category"});
    // Fulltext wird im Content Benchmark lazy erstellt
    // Räumlicher Index
    g_spatialIdx->createSpatialIndex("hotels", "location");

    // Vector Index initialisieren
    g_vectorIdx->init("hotels", static_cast<int>(VECTOR_DIM));

    std::mt19937 rng(42);
    std::uniform_real_distribution<double> lon_dist(13.0, 13.8);
    std::uniform_real_distribution<double> lat_dist(52.3, 52.7);
    std::uniform_int_distribution<int> stars_dist(1, 5);
    std::uniform_real_distribution<float> vec_dist(-1.0f, 1.0f);

    std::vector<std::string> cities = {"Berlin", "Munich", "Hamburg"};
    std::vector<std::string> categories = {"budget", "mid-range", "luxury"};

    for (size_t i = 0; i < NUM_HOTELS; ++i) {
        BaseEntity::FieldMap fields;
        fields["name"] = std::string("Hotel_" + std::to_string(i));
        fields["city"] = cities[i % cities.size()];
        fields["category"] = categories[i % categories.size()];
        fields["stars"] = static_cast<int64_t>(stars_dist(rng));
        // Embedding
        std::vector<float> embedding(VECTOR_DIM); for (auto &v : embedding) v = vec_dist(rng); fields["embedding"] = embedding;
        // Geo: Punkt
        double lon = lon_dist(rng); double lat = lat_dist(rng);
        // Für vereinfachte Speicherung: separate Felder lon/lat und GeoJSON serialisiert
        fields["lon"] = lon;
        fields["lat"] = lat;
        // Entity anlegen
        BaseEntity entity("hotel_" + std::to_string(i), fields);
        // GeoSidecar erstellen und am Entity setzen
        geo::MBR mbr(lon, lat, lon, lat);
        geo::GeoSidecar sidecar(mbr);
        entity.setGeoSidecar(sidecar);
        // Persist & Indizes pflegen
        g_secIdx->put("hotels", entity);
        g_vectorIdx->addEntity(entity, "embedding");
        g_spatialIdx->insert("hotels", entity.getPrimaryKey(), sidecar);
    }

    THEMIS_INFO("Benchmark test data setup complete: {} hotels", NUM_HOTELS);
}

static void TeardownTestData() {
    delete g_engine;
    delete g_graphIdx;
    delete g_spatialIdx;
    delete g_vectorIdx;
    delete g_secIdx;
    delete g_db;
    
    // Cleanup temp DB
    std::filesystem::remove_all("bench_hybrid_aql_tmp.db");
}

// ============================================================================
// Benchmark: Vector+Geo via AQL Sugar
// ============================================================================

static void BM_VectorGeo_AQL_Sugar(benchmark::State& state) {
    std::string aql = R"(
        FOR doc IN hotels
          FILTER ST_Within(doc.location, [13.3, 52.4, 13.7, 52.6])
          FILTER doc.city == "Berlin"
          SORT SIMILARITY(doc.embedding, @queryVec) DESC
          LIMIT 10
          RETURN doc
    )";
    
    // Query vector
    std::vector<float> queryVec(VECTOR_DIM, 0.5f);
    
    for (auto _ : state) {
        auto [st, result] = executeAql(aql, *g_engine);
        benchmark::DoNotOptimize(result);
        if (!st.ok) state.SkipWithError(st.message.c_str());
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["hotels"] = NUM_HOTELS;
}
BENCHMARK(BM_VectorGeo_AQL_Sugar)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Vector+Geo via Direct C++ API
// ============================================================================

static void BM_VectorGeo_CPP_API(benchmark::State& state) {
    std::vector<float> queryVec(VECTOR_DIM, 0.5f);
    
    VectorGeoQuery q;
    q.table = "hotels";
    q.vector_field = "embedding";
    q.geom_field = "location";
    q.query_vector = queryVec;
    q.k = 10;
    
    // Build spatial filter AST
    auto bbox = std::make_shared<query::ArrayLiteralExpr>(std::vector<std::shared_ptr<query::Expression>>{
        std::make_shared<query::LiteralExpr>(13.3),
        std::make_shared<query::LiteralExpr>(52.4),
        std::make_shared<query::LiteralExpr>(13.7),
        std::make_shared<query::LiteralExpr>(52.6)
    });
    auto loc = std::make_shared<query::FieldAccessExpr>(std::make_shared<query::VariableExpr>("doc"), "location");
    auto spatialCall = std::make_shared<query::FunctionCallExpr>("ST_Within", std::vector<std::shared_ptr<query::Expression>>{loc, bbox});
    q.spatial_filter = spatialCall;
    
    // Equality filter
    auto city_fa = std::make_shared<query::FieldAccessExpr>(std::make_shared<query::VariableExpr>("doc"), "city");
    auto city_lit = std::make_shared<query::LiteralExpr>(std::string("Berlin"));
    auto city_eq = std::make_shared<query::BinaryOpExpr>(query::BinaryOperator::Eq, city_fa, city_lit);
    q.extra_filters.push_back(city_eq);
    
    for (auto _ : state) {
        auto [st, result] = g_engine->executeVectorGeoQuery(q);
        benchmark::DoNotOptimize(result);
        if (!st.ok) state.SkipWithError(st.message.c_str());
    }
    
    state.SetItemsProcessed(state.iterations());
    state.counters["hotels"] = NUM_HOTELS;
}
BENCHMARK(BM_VectorGeo_CPP_API)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Content+Geo via AQL Sugar
// ============================================================================

static void BM_ContentGeo_AQL_Sugar(benchmark::State& state) {
    // Setup fulltext index first
    static bool ftSetup = false;
    if (!ftSetup) {
        g_secIdx->createFulltextIndex("hotels", "name");
        ftSetup = true;
    }
    
    std::string aql = R"(
        FOR doc IN hotels
          FILTER FULLTEXT(doc.name, "Hotel", 100)
          FILTER ST_Within(doc.location, [13.3, 52.4, 13.7, 52.6])
          SORT PROXIMITY(doc.location, [13.5, 52.52]) ASC
          LIMIT 20
          RETURN doc
    )";
    
    for (auto _ : state) {
        auto [st, result] = executeAql(aql, *g_engine);
        benchmark::DoNotOptimize(result);
        if (!st.ok) state.SkipWithError(st.message.c_str());
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ContentGeo_AQL_Sugar)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Content+Geo via Direct C++ API
// ============================================================================

static void BM_ContentGeo_CPP_API(benchmark::State& state) {
    ContentGeoQuery q;
    q.table = "hotels";
    q.text_field = "name";
    q.fulltext_query = "Hotel";
    q.geom_field = "location";
    q.limit = 20;
    q.boost_by_distance = true;
    q.center_point = std::vector<float>{13.5f, 52.52f};
    
    // Spatial filter
    auto bbox = std::make_shared<query::ArrayLiteralExpr>(std::vector<std::shared_ptr<query::Expression>>{
        std::make_shared<query::LiteralExpr>(13.3),
        std::make_shared<query::LiteralExpr>(52.4),
        std::make_shared<query::LiteralExpr>(13.7),
        std::make_shared<query::LiteralExpr>(52.6)
    });
    auto loc = std::make_shared<query::FieldAccessExpr>(std::make_shared<query::VariableExpr>("doc"), "location");
    auto spatialCall = std::make_shared<query::FunctionCallExpr>("ST_Within", std::vector<std::shared_ptr<query::Expression>>{loc, bbox});
    q.spatial_filter = spatialCall;
    
    for (auto _ : state) {
        auto [st, result] = g_engine->executeContentGeoQuery(q);
        benchmark::DoNotOptimize(result);
        if (!st.ok) state.SkipWithError(st.message.c_str());
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_ContentGeo_CPP_API)->Unit(benchmark::kMillisecond);

// ============================================================================
// Benchmark: Plan Overhead (Parsing + Translation)
// ============================================================================

static void BM_AQL_Parse_Translate_Only(benchmark::State& state) {
    std::string aql = R"(
        FOR doc IN hotels
          FILTER ST_Within(doc.location, [13.3, 52.4, 13.7, 52.6])
          SORT SIMILARITY(doc.embedding, @vec) DESC
          LIMIT 10
          RETURN doc
    )";
    
    for (auto _ : state) {
        query::AQLParser parser;
        auto parseResult = parser.parse(aql);
        if (!parseResult.success) {
            state.SkipWithError(parseResult.error.message.c_str());
            continue;
        }
        benchmark::DoNotOptimize(parseResult.query);
        auto tr = AQLTranslator::translate(parseResult.query);
        benchmark::DoNotOptimize(tr);
    }
    
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AQL_Parse_Translate_Only)->Unit(benchmark::kMicrosecond);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    themis::utils::Logger::init();
    themis::Tracer::initialize("bench_hybrid_aql", "http://127.0.0.1:4318");
    THEMIS_INFO("Starting Hybrid AQL Benchmark Suite (hybrid queries)");
    SetupTestData();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
    TeardownTestData();
    ::benchmark::Shutdown();
    THEMIS_INFO("Benchmark suite completed");
    return 0;
}
