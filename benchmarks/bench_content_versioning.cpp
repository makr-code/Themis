// Benchmark: Content Version Management Performance
// Tests version creation, diff computation, and retrieval performance

#include <benchmark/benchmark.h>
#include <string>
#include <vector>
#include <random>
#include <chrono>

// Mock content versioning system
class ContentVersionStore {
public:
    struct Version {
        std::string version_id;
        std::string content;
        std::string diff_from_previous;
        size_t size;
        std::chrono::system_clock::time_point created_at;
    };
    
    std::string create_version(const std::string& content) {
        std::string version_id = "v" + std::to_string(versions_.size() + 1);
        Version v;
        v.version_id = version_id;
        v.content = content;
        v.size = content.size();
        v.created_at = std::chrono::system_clock::now();
        
        if (!versions_.empty()) {
            v.diff_from_previous = compute_diff(versions_.back().content, content);
        }
        
        versions_.push_back(v);
        return version_id;
    }
    
    Version get_version(const std::string& version_id) {
        for (const auto& v : versions_) {
            if (v.version_id == version_id) return v;
        }
        throw std::runtime_error("Version not found");
    }
    
    std::string compute_diff(const std::string& old_content, const std::string& new_content) {
        // Simplified diff computation (mock implementation)
        size_t common_prefix = 0;
        size_t min_len = std::min(old_content.size(), new_content.size());
        
        while (common_prefix < min_len && old_content[common_prefix] == new_content[common_prefix]) {
            common_prefix++;
        }
        
        return new_content.substr(common_prefix);
    }
    
    size_t get_storage_overhead() {
        size_t total_size = 0;
        for (const auto& v : versions_) {
            total_size += v.size + v.diff_from_previous.size();
        }
        return total_size;
    }
    
private:
    std::vector<Version> versions_;
};

// Generate random content of specified size
std::string generate_content(size_t size) {
    static std::mt19937 gen(42);
    static std::uniform_int_distribution<> dis(32, 126);
    
    std::string content;
    content.reserve(size);
    for (size_t i = 0; i < size; ++i) {
        content += static_cast<char>(dis(gen));
    }
    return content;
}

// Benchmark: Version creation latency for different file sizes
static void BM_VersionCreation(benchmark::State& state) {
    size_t file_size = state.range(0);
    ContentVersionStore store;
    std::string content = generate_content(file_size);
    
    for (auto _ : state) {
        std::string version_id = store.create_version(content);
        benchmark::DoNotOptimize(version_id);
    }
    
    state.SetBytesProcessed(state.iterations() * file_size);
}
BENCHMARK(BM_VersionCreation)->Arg(1024)->Arg(10*1024)->Arg(100*1024)->Arg(1024*1024)->Arg(10*1024*1024);

// Benchmark: Diff computation performance
static void BM_DiffComputation(benchmark::State& state) {
    size_t file_size = state.range(0);
    ContentVersionStore store;
    std::string old_content = generate_content(file_size);
    std::string new_content = generate_content(file_size);
    
    for (auto _ : state) {
        std::string diff = store.compute_diff(old_content, new_content);
        benchmark::DoNotOptimize(diff);
    }
    
    state.SetBytesProcessed(state.iterations() * file_size * 2);
}
BENCHMARK(BM_DiffComputation)->Arg(1024)->Arg(10*1024)->Arg(100*1024)->Arg(1024*1024);

// Benchmark: Version retrieval latency
static void BM_VersionRetrieval(benchmark::State& state) {
    ContentVersionStore store;
    std::vector<std::string> version_ids;
    
    // Create 100 versions
    for (int i = 0; i < 100; ++i) {
        std::string content = generate_content(1024);
        version_ids.push_back(store.create_version(content));
    }
    
    size_t idx = 0;
    for (auto _ : state) {
        auto version = store.get_version(version_ids[idx % version_ids.size()]);
        benchmark::DoNotOptimize(version);
        idx++;
    }
}
BENCHMARK(BM_VersionRetrieval);

// Benchmark: Storage overhead analysis
static void BM_StorageOverhead(benchmark::State& state) {
    size_t num_versions = state.range(0);
    
    for (auto _ : state) {
        ContentVersionStore store;
        for (size_t i = 0; i < num_versions; ++i) {
            std::string content = generate_content(1024);
            store.create_version(content);
        }
        
        size_t overhead = store.get_storage_overhead();
        benchmark::DoNotOptimize(overhead);
    }
}
BENCHMARK(BM_StorageOverhead)->Arg(10)->Arg(50)->Arg(100)->Arg(500);

// Benchmark: Concurrent version operations
static void BM_ConcurrentVersioning(benchmark::State& state) {
    ContentVersionStore store;
    std::string content = generate_content(10 * 1024);
    
    for (auto _ : state) {
        store.create_version(content);
    }
    
    state.SetBytesProcessed(state.iterations() * content.size());
}
BENCHMARK(BM_ConcurrentVersioning)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

BENCHMARK_MAIN();
