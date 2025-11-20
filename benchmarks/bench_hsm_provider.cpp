// HSM Provider Performance Benchmark
// Tests sign/verify throughput with different session pool sizes
//
// Build:
//   cmake -S . -B build -G Ninja -DTHEMIS_BUILD_BENCHMARKS=ON -DTHEMIS_ENABLE_HSM_REAL=ON
//   cmake --build build --target bench_hsm_provider -j
//
// Run:
//   export THEMIS_TEST_HSM_LIBRARY=/usr/lib/softhsm/libsofthsm2.so
//   export THEMIS_TEST_HSM_PIN=1234
//   ./build/bench_hsm_provider --benchmark_filter=HSM

#include <benchmark/benchmark.h>
#include "security/hsm_provider.h"
#include <filesystem>
#include <random>

using namespace themis::security;

static std::string getHSMLibPath() {
    if(const char* env = std::getenv("THEMIS_TEST_HSM_LIBRARY")) return env;
    std::vector<std::string> paths = {
        "/usr/lib/softhsm/libsofthsm2.so",
        "/usr/lib/x86_64-linux-gnu/softhsm/libsofthsm2.so",
        "/usr/local/lib/softhsm/libsofthsm2.so",
        "/opt/homebrew/lib/softhsm/libsofthsm2.so"
    };
    for(auto& p: paths) if(std::filesystem::exists(p)) return p;
    return "";
}

static HSMConfig makeConfig(uint32_t poolSize) {
    HSMConfig cfg;
    cfg.library_path = getHSMLibPath();
    cfg.slot_id = 0;
    cfg.pin = std::getenv("THEMIS_TEST_HSM_PIN") ? std::getenv("THEMIS_TEST_HSM_PIN") : "1234";
    cfg.key_label = "themis-signing-key";
    cfg.signature_algorithm = "RSA-SHA256";
    cfg.verbose = false;
    cfg.session_pool_size = poolSize;
    return cfg;
}

static std::vector<uint8_t> randomData(size_t size) {
    static std::mt19937 rng(42);
    std::vector<uint8_t> data(size);
    for(auto& b: data) b = rng() & 0xFF;
    return data;
}

// Baseline: Stub provider (no PKCS#11)
static void BM_HSM_Sign_Stub(benchmark::State& state) {
    HSMConfig cfg; cfg.library_path = ""; // force stub
    HSMProvider hsm(cfg);
    if(!hsm.initialize()){ state.SkipWithError("Init failed"); return; }
    auto data = randomData(256);
    for(auto _ : state) {
        auto sig = hsm.sign(data);
        benchmark::DoNotOptimize(sig);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Sign_Stub);

static void BM_HSM_Verify_Stub(benchmark::State& state) {
    HSMConfig cfg; cfg.library_path = "";
    HSMProvider hsm(cfg); hsm.initialize();
    auto data = randomData(256);
    auto sig = hsm.sign(data);
    for(auto _ : state) {
        bool ok = hsm.verify(data, sig.signature_b64);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Verify_Stub);

// Real PKCS#11: Single session
static void BM_HSM_Sign_Real_Pool1(benchmark::State& state) {
    auto cfg = makeConfig(1);
    if(cfg.library_path.empty()){ state.SkipWithError("HSM lib not found"); return; }
    HSMProvider hsm(cfg);
    if(!hsm.initialize()){ state.SkipWithError("Init failed"); return; }
    auto data = randomData(256);
    for(auto _ : state) {
        auto sig = hsm.sign(data);
        benchmark::DoNotOptimize(sig);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Sign_Real_Pool1);

// Pool size 2
static void BM_HSM_Sign_Real_Pool2(benchmark::State& state) {
    auto cfg = makeConfig(2);
    if(cfg.library_path.empty()){ state.SkipWithError("HSM lib not found"); return; }
    HSMProvider hsm(cfg); if(!hsm.initialize()){ state.SkipWithError("Init failed"); return; }
    auto data = randomData(256);
    for(auto _ : state) {
        auto sig = hsm.sign(data);
        benchmark::DoNotOptimize(sig);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Sign_Real_Pool2);

// Pool size 4
static void BM_HSM_Sign_Real_Pool4(benchmark::State& state) {
    auto cfg = makeConfig(4);
    if(cfg.library_path.empty()){ state.SkipWithError("HSM lib not found"); return; }
    HSMProvider hsm(cfg); if(!hsm.initialize()){ state.SkipWithError("Init failed"); return; }
    auto data = randomData(256);
    for(auto _ : state) {
        auto sig = hsm.sign(data);
        benchmark::DoNotOptimize(sig);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Sign_Real_Pool4);

// Verify with pool 4
static void BM_HSM_Verify_Real_Pool4(benchmark::State& state) {
    auto cfg = makeConfig(4);
    if(cfg.library_path.empty()){ state.SkipWithError("HSM lib not found"); return; }
    HSMProvider hsm(cfg); if(!hsm.initialize()){ state.SkipWithError("Init failed"); return; }
    auto data = randomData(256);
    auto sig = hsm.sign(data);
    for(auto _ : state) {
        bool ok = hsm.verify(data, sig.signature_b64);
        benchmark::DoNotOptimize(ok);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Verify_Real_Pool4);

// Parallel sign benchmark (threads)
static void BM_HSM_Sign_Parallel(benchmark::State& state) {
    auto cfg = makeConfig(4);
    if(cfg.library_path.empty()){ state.SkipWithError("HSM lib not found"); return; }
    static HSMProvider hsm(cfg);
    static bool initialized = false;
    if(!initialized){ hsm.initialize(); initialized = true; }
    auto data = randomData(256);
    for(auto _ : state) {
        auto sig = hsm.sign(data);
        benchmark::DoNotOptimize(sig);
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_HSM_Sign_Parallel)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

BENCHMARK_MAIN();
