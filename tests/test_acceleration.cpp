#include <gtest/gtest.h>
#include "acceleration/compute_backend.h"
#include "acceleration/cpu_backend.h"
#include <vector>
#include <cmath>

using namespace themis::acceleration;

class AccelerationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register CPU backend (always available)
        auto cpuVector = std::make_unique<CPUVectorBackend>();
        BackendRegistry::instance().registerBackend(std::move(cpuVector));
        
        auto cpuGraph = std::make_unique<CPUGraphBackend>();
        BackendRegistry::instance().registerBackend(std::move(cpuGraph));
        
        auto cpuGeo = std::make_unique<CPUGeoBackend>();
        BackendRegistry::instance().registerBackend(std::move(cpuGeo));
    }
    
    void TearDown() override {
        BackendRegistry::instance().shutdownAll();
    }
};

TEST_F(AccelerationTest, BackendRegistryBasics) {
    auto& registry = BackendRegistry::instance();
    
    // CPU backend should always be available
    auto* cpuBackend = registry.getBackend(BackendType::CPU);
    ASSERT_NE(cpuBackend, nullptr);
    EXPECT_TRUE(cpuBackend->isAvailable());
    EXPECT_STREQ(cpuBackend->name(), "CPU");
}

TEST_F(AccelerationTest, CPUVectorBackend) {
    auto& registry = BackendRegistry::instance();
    auto* backend = registry.getBestVectorBackend();
    
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->initialize());
    
    // Test data: 3 vectors in 2D space
    std::vector<float> vectors = {
        1.0f, 0.0f,  // Vector 0
        0.0f, 1.0f,  // Vector 1
        1.0f, 1.0f   // Vector 2
    };
    
    // Query vector
    std::vector<float> query = {0.5f, 0.5f};
    
    // Compute distances
    auto distances = backend->computeDistances(
        query.data(), 1, 2,
        vectors.data(), 3,
        true  // Use L2
    );
    
    ASSERT_EQ(distances.size(), 3);
    
    // Verify distances are reasonable
    for (float dist : distances) {
        EXPECT_GE(dist, 0.0f);
        EXPECT_LT(dist, 10.0f);
    }
    
    backend->shutdown();
}

TEST_F(AccelerationTest, CPUVectorKnnSearch) {
    auto& registry = BackendRegistry::instance();
    auto* backend = registry.getBestVectorBackend();
    
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->initialize());
    
    // Test data: 5 vectors in 3D space
    std::vector<float> vectors = {
        1.0f, 0.0f, 0.0f,  // Vector 0
        0.0f, 1.0f, 0.0f,  // Vector 1
        0.0f, 0.0f, 1.0f,  // Vector 2
        1.0f, 1.0f, 0.0f,  // Vector 3
        1.0f, 1.0f, 1.0f   // Vector 4
    };
    
    // Query: origin
    std::vector<float> query = {0.0f, 0.0f, 0.0f};
    
    // Find 3 nearest neighbors
    auto results = backend->batchKnnSearch(
        query.data(), 1, 3,
        vectors.data(), 5,
        3,  // k=3
        true  // Use L2
    );
    
    ASSERT_EQ(results.size(), 1);
    ASSERT_EQ(results[0].size(), 3);
    
    // Results should be sorted by distance (ascending)
    for (size_t i = 1; i < results[0].size(); ++i) {
        EXPECT_LE(results[0][i-1].second, results[0][i].second);
    }
    
    backend->shutdown();
}

TEST_F(AccelerationTest, CPUGeoBackend) {
    auto& registry = BackendRegistry::instance();
    auto* backend = registry.getBestGeoBackend();
    
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->initialize());
    
    // Test data: distances between cities
    std::vector<double> lats1 = {51.5074};  // London
    std::vector<double> lons1 = {-0.1278};
    std::vector<double> lats2 = {48.8566};  // Paris
    std::vector<double> lons2 = {2.3522};
    
    auto distances = backend->batchDistances(
        lats1.data(), lons1.data(),
        lats2.data(), lons2.data(),
        1, true  // Use Haversine
    );
    
    ASSERT_EQ(distances.size(), 1);
    
    // London-Paris distance should be around 340-350 km
    EXPECT_GT(distances[0], 300.0f);
    EXPECT_LT(distances[0], 400.0f);
    
    backend->shutdown();
}

TEST_F(AccelerationTest, CPUGeoPointInPolygon) {
    auto& registry = BackendRegistry::instance();
    auto* backend = registry.getBestGeoBackend();
    
    ASSERT_NE(backend, nullptr);
    EXPECT_TRUE(backend->initialize());
    
    // Simple square polygon: (0,0), (10,0), (10,10), (0,10)
    std::vector<double> polygon = {
        0.0, 0.0,
        10.0, 0.0,
        10.0, 10.0,
        0.0, 10.0
    };
    
    // Test points: inside, outside, on edge
    std::vector<double> pointLats = {5.0, 15.0, 0.0};
    std::vector<double> pointLons = {5.0, 5.0, 0.0};
    
    auto results = backend->batchPointInPolygon(
        pointLats.data(), pointLons.data(), 3,
        polygon.data(), 4
    );
    
    ASSERT_EQ(results.size(), 3);
    EXPECT_TRUE(results[0]);   // (5,5) is inside
    EXPECT_FALSE(results[1]);  // (15,5) is outside
    // Point on edge behavior is implementation-specific
    
    backend->shutdown();
}

TEST_F(AccelerationTest, BackendCapabilities) {
    auto& registry = BackendRegistry::instance();
    
    auto* vectorBackend = registry.getBestVectorBackend();
    ASSERT_NE(vectorBackend, nullptr);
    
    auto caps = vectorBackend->getCapabilities();
    EXPECT_TRUE(caps.supportsVectorOps);
    EXPECT_TRUE(caps.supportsBatchProcessing);
    EXPECT_FALSE(caps.deviceName.empty());
}

TEST_F(AccelerationTest, GracefulDegradation) {
    auto& registry = BackendRegistry::instance();
    
    // Try to get CUDA backend (will not be available in CPU-only builds)
    auto* cudaBackend = registry.getBackend(BackendType::CUDA);
    
    // Should fall back to best available (CPU)
    auto* bestBackend = registry.getBestVectorBackend();
    ASSERT_NE(bestBackend, nullptr);
    
    // Should work regardless of which backend is used
    EXPECT_TRUE(bestBackend->initialize());
    EXPECT_TRUE(bestBackend->isAvailable());
}

// Benchmark test (disabled by default)
TEST_F(AccelerationTest, DISABLED_VectorSearchBenchmark) {
    auto& registry = BackendRegistry::instance();
    auto* backend = registry.getBestVectorBackend();
    
    ASSERT_NE(backend, nullptr);
    backend->initialize();
    
    const size_t numVectors = 10000;
    const size_t dim = 128;
    const size_t numQueries = 100;
    const size_t k = 10;
    
    // Generate random vectors
    std::vector<float> vectors(numVectors * dim);
    for (auto& v : vectors) {
        v = static_cast<float>(rand()) / RAND_MAX;
    }
    
    std::vector<float> queries(numQueries * dim);
    for (auto& q : queries) {
        q = static_cast<float>(rand()) / RAND_MAX;
    }
    
    auto start = std::chrono::high_resolution_clock::now();
    
    auto results = backend->batchKnnSearch(
        queries.data(), numQueries, dim,
        vectors.data(), numVectors,
        k, true
    );
    
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    std::cout << "Backend: " << backend->name() << std::endl;
    std::cout << "Time: " << duration.count() << " ms" << std::endl;
    std::cout << "Queries/sec: " << (numQueries * 1000.0 / duration.count()) << std::endl;
    
    EXPECT_EQ(results.size(), numQueries);
    
    backend->shutdown();
}
