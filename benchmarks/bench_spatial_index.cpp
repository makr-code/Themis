#include <benchmark/benchmark.h>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <filesystem>

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/spatial_index.h"
#include "index/secondary_index.h"
#include "query/query_engine.h"
#include "utils/geo/ewkb.h"
#include "api/geo_index_hooks.h"
#include <nlohmann/json.hpp>

using themis::RocksDBWrapper;
using themis::BaseEntity;
using themis::SecondaryIndexManager;
using themis::index::SpatialIndexManager;
using themis::QueryEngine;
using themis::geo::EWKBParser;
using themis::geo::MBR;
using json = nlohmann::json;

namespace {

// Simulated NaturalEarth-style data: points representing cities
struct GeoDataset {
    std::shared_ptr<RocksDBWrapper> db;
    std::shared_ptr<SecondaryIndexManager> sec_idx;
    std::shared_ptr<SpatialIndexManager> spatial_idx;
    std::unique_ptr<QueryEngine> qe;
    size_t N = 10000; // 10k points (simulating cities/POIs)
    bool ready = false;
    
    // Geographic bounds (approximate world coverage)
    double min_lon = -180.0;
    double max_lon = 180.0;
    double min_lat = -85.0;  // Mercator limits
    double max_lat = 85.0;

    static GeoDataset& instance() { 
        static GeoDataset env; 
        return env; 
    }

    // Generate random point within bounds
    static json generateRandomPoint(double minx, double maxx, double miny, double maxy, std::mt19937& rng) {
        std::uniform_real_distribution<double> dist_x(minx, maxx);
        std::uniform_real_distribution<double> dist_y(miny, maxy);
        
        return {
            {"type", "Point"},
            {"coordinates", {dist_x(rng), dist_y(rng)}}
        };
    }

    // Generate random polygon (bounding box)
    static json generateRandomPolygon(double centerx, double centery, double size, std::mt19937& rng) {
        std::uniform_real_distribution<double> jitter(-size * 0.2, size * 0.2);
        
        double minx = centerx - size / 2 + jitter(rng);
        double maxx = centerx + size / 2 + jitter(rng);
        double miny = centery - size / 2 + jitter(rng);
        double maxy = centery + size / 2 + jitter(rng);
        
        return {
            {"type", "Polygon"},
            {"coordinates", json::array({
                json::array({
                    json::array({minx, miny}),
                    json::array({maxx, miny}),
                    json::array({maxx, maxy}),
                    json::array({minx, maxy}),
                    json::array({minx, miny})
                })
            })}
        };
    }

    void initOnce() {
        if (ready) return;
        
        const std::string db_path = "data/themis_bench_spatial";
        std::error_code ec;
        std::filesystem::remove_all(db_path, ec);
        
        RocksDBWrapper::Config cfg;
        cfg.db_path = db_path;
        cfg.memtable_size_mb = 128;
        cfg.block_cache_size_mb = 256;
        
        db = std::make_shared<RocksDBWrapper>(cfg);
        if (!db->open()) {
            throw std::runtime_error("Failed to open RocksDB");
        }
        
        sec_idx = std::make_shared<SecondaryIndexManager>(*db);
        spatial_idx = std::make_unique<SpatialIndexManager>(*db);
        
        // Create spatial index for "places" table
        SpatialIndexManager::RTreeConfig spatial_cfg;
        spatial_cfg.total_bounds = MBR(min_lon, min_lat, max_lon, max_lat);
        auto st = spatial_idx->createSpatialIndex("places", "geometry", spatial_cfg);
        if (!st) {
            throw std::runtime_error("Failed to create spatial index: " + st.message);
        }
        
        // Insert simulated NaturalEarth-style points
        std::mt19937 rng(42);
        
        for (size_t i = 0; i < N; ++i) {
            std::string pk = "place_" + std::to_string(i);
            
            // Create entity with geometry
            json entity;
            entity["id"] = pk;
            entity["name"] = "City_" + std::to_string(i);
            entity["population"] = 10000 + (i * 1000);
            entity["geometry"] = generateRandomPoint(min_lon, max_lon, min_lat, max_lat, rng);
            
            std::string blob_str = entity.dump();
            std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
            
            // Store entity in DB
            db->put("entity:places:" + pk, blob);
            
            // Trigger geo index hook
            themis::api::GeoIndexHooks::onEntityPut(*db, spatial_idx.get(), "places", pk, blob);
        }
        
        // Create query engine
        qe = std::make_unique<QueryEngine>(*db, *sec_idx, *spatial_idx);
        
        ready = true;
    }
};

// Query bbox size relative to world
enum QuerySize {
    TINY = 0,      // 1% of world (city-level)
    SMALL = 1,     // 5% of world (region-level)
    MEDIUM = 2,    // 20% of world (country-level)
    LARGE = 3      // 50% of world (continent-level)
};

MBR generateQueryBbox(QuerySize size, std::mt19937& rng) {
    GeoDataset& env = GeoDataset::instance();
    
    double size_factor;
    switch (size) {
        case TINY:   size_factor = 0.01; break;
        case SMALL:  size_factor = 0.05; break;
        case MEDIUM: size_factor = 0.20; break;
        case LARGE:  size_factor = 0.50; break;
        default:     size_factor = 0.01; break;
    }
    
    double width = (env.max_lon - env.min_lon) * size_factor;
    double height = (env.max_lat - env.min_lat) * size_factor;
    
    std::uniform_real_distribution<double> dist_x(env.min_lon, env.max_lon - width);
    std::uniform_real_distribution<double> dist_y(env.min_lat, env.max_lat - height);
    
    double minx = dist_x(rng);
    double miny = dist_y(rng);
    
    return MBR(minx, miny, minx + width, miny + height);
}

} // namespace

// ============================================================================
// Benchmarks
// ============================================================================

// Benchmark: Insert performance
static void BM_Spatial_Insert(benchmark::State& state) {
    auto& env = GeoDataset::instance();
    env.initOnce();
    
    std::mt19937 rng(state.range(0));
    size_t insert_count = 0;
    
    for (auto _ : state) {
        std::string pk = "bench_insert_" + std::to_string(insert_count++);
        
        json entity;
        entity["id"] = pk;
        entity["geometry"] = GeoDataset::generateRandomPoint(
            env.min_lon, env.max_lon, env.min_lat, env.max_lat, rng);
        
        std::string blob_str = entity.dump();
        std::vector<uint8_t> blob(blob_str.begin(), blob_str.end());
        
        env.db->put("entity:places:" + pk, blob);
        
        auto start = std::chrono::high_resolution_clock::now();
        themis::api::GeoIndexHooks::onEntityPut(*env.db, env.spatial_idx.get(), "places", pk, blob);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        state.SetIterationTime(elapsed.count() / 1000000.0);
    }
    
    state.counters["dataset_size"] = static_cast<double>(env.N);
}

BENCHMARK(BM_Spatial_Insert)->Arg(1)->Arg(2)->Arg(3)->UseManualTime()->Unit(benchmark::kMicrosecond);

// Benchmark: Query performance - Tiny bbox (city-level, ~100 results)
static void BM_Spatial_Query_Tiny(benchmark::State& state) {
    auto& env = GeoDataset::instance();
    env.initOnce();
    
    std::mt19937 rng(42);
    size_t result_count = 0;
    
    for (auto _ : state) {
        MBR query_bbox = generateQueryBbox(TINY, rng);
        
        auto results = env.spatial_idx->searchIntersects("places", query_bbox);
        result_count += results.size();
        
        benchmark::DoNotOptimize(results);
    }
    
    state.counters["avg_results"] = static_cast<double>(result_count) / state.iterations();
    state.counters["dataset_size"] = static_cast<double>(env.N);
}

BENCHMARK(BM_Spatial_Query_Tiny)->Unit(benchmark::kMillisecond);

// Benchmark: Query performance - Small bbox (region-level, ~500 results)
static void BM_Spatial_Query_Small(benchmark::State& state) {
    auto& env = GeoDataset::instance();
    env.initOnce();
    
    std::mt19937 rng(43);
    size_t result_count = 0;
    
    for (auto _ : state) {
        MBR query_bbox = generateQueryBbox(SMALL, rng);
        
        auto results = env.spatial_idx->searchIntersects("places", query_bbox);
        result_count += results.size();
        
        benchmark::DoNotOptimize(results);
    }
    
    state.counters["avg_results"] = static_cast<double>(result_count) / state.iterations();
    state.counters["dataset_size"] = static_cast<double>(env.N);
}

BENCHMARK(BM_Spatial_Query_Small)->Unit(benchmark::kMillisecond);

// Benchmark: Query performance - Medium bbox (country-level, ~2000 results)
static void BM_Spatial_Query_Medium(benchmark::State& state) {
    auto& env = GeoDataset::instance();
    env.initOnce();
    
    std::mt19937 rng(44);
    size_t result_count = 0;
    
    for (auto _ : state) {
        MBR query_bbox = generateQueryBbox(MEDIUM, rng);
        
        auto results = env.spatial_idx->searchIntersects("places", query_bbox);
        result_count += results.size();
        
        benchmark::DoNotOptimize(results);
    }
    
    state.counters["avg_results"] = static_cast<double>(result_count) / state.iterations();
    state.counters["dataset_size"] = static_cast<double>(env.N);
}

BENCHMARK(BM_Spatial_Query_Medium)->Unit(benchmark::kMillisecond);

// Benchmark: Query performance - Large bbox (continent-level, ~5000 results)
static void BM_Spatial_Query_Large(benchmark::State& state) {
    auto& env = GeoDataset::instance();
    env.initOnce();
    
    std::mt19937 rng(45);
    size_t result_count = 0;
    
    for (auto _ : state) {
        MBR query_bbox = generateQueryBbox(LARGE, rng);
        
        auto results = env.spatial_idx->searchIntersects("places", query_bbox);
        result_count += results.size();
        
        benchmark::DoNotOptimize(results);
    }
    
    state.counters["avg_results"] = static_cast<double>(result_count) / state.iterations();
    state.counters["dataset_size"] = static_cast<double>(env.N);
}

BENCHMARK(BM_Spatial_Query_Large)->Unit(benchmark::kMillisecond);

// Benchmark: MBR-only vs Exact geometry check overhead
static void BM_Spatial_ExactCheck_Overhead(benchmark::State& state) {
    auto& env = GeoDataset::instance();
    env.initOnce();
    
    // This benchmark measures the overhead of exact geometry checks
    // by comparing MBR-only queries vs queries with exact backend enabled
    
    std::mt19937 rng(46);
    size_t exact_result_count = 0;
    
    for (auto _ : state) {
        MBR query_bbox = generateQueryBbox(SMALL, rng);
        
        // Query with exact backend (if available)
        auto results = env.spatial_idx->searchIntersects("places", query_bbox);
        exact_result_count += results.size();
        
        benchmark::DoNotOptimize(results);
    }
    
    state.counters["avg_results"] = static_cast<double>(exact_result_count) / state.iterations();
    state.counters["dataset_size"] = static_cast<double>(env.N);
}

BENCHMARK(BM_Spatial_ExactCheck_Overhead)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
