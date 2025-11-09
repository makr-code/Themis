#include <benchmark/benchmark.h>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include "utils/zstd_codec.h"

using namespace std;

static string genText(size_t bytes){
    string s; s.reserve(bytes);
    static const string words[] = {"lorem","ipsum","dolor","sit","amet","consectetur","adipiscing","elit"};
    size_t i=0; while (s.size()<bytes){ s += words[i%8]; s += ' '; ++i; }
    s.resize(bytes);
    return s;
}

static void BM_ZstdLevels(benchmark::State& state){
    int level = static_cast<int>(state.range(0));
    size_t size = static_cast<size_t>(state.range(1));
    string src = genText(size);
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(src.data());
    vector<uint8_t> out;
    for (auto _ : state){
        benchmark::DoNotOptimize(ptr);
        out = themis::utils::zstd_compress(ptr, src.size(), level);
        benchmark::ClobberMemory();
    }
    state.counters["ratio"] = benchmark::Counter(static_cast<double>(src.size()) / (out.empty()? (double)src.size() : (double)out.size()));
}

// Levels to test: 3, 9, 19 on 16KB and 128KB
BENCHMARK(BM_ZstdLevels)->Args({3, 16384})->Args({9, 16384})->Args({19, 16384})
                         ->Args({3, 131072})->Args({9, 131072})->Args({19, 131072})
                         ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
