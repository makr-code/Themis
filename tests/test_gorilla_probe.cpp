#include <gtest/gtest.h>
#include "timeseries/gorilla.h"
#include <cstdio>
#include <vector>
#include <cmath>

using namespace themis;

TEST(GorillaProbe, FindFirstMismatch) {
    std::vector<std::pair<int64_t,double>> series;
    int64_t t0 = 1700000000000LL;
    for (int i = 0; i < 1000; ++i) {
        series.emplace_back(t0 + i * 1000, i == 0 ? 0.0 : std::sin(i * 0.01));
    }
    
    GorillaEncoder enc;
    for (auto &p : series) enc.add(p.first, p.second);
    auto bytes = enc.finish();

    GorillaDecoder dec(bytes);
    for (size_t i = 0; i < series.size(); ++i) {
        auto nxt = dec.next();
        ASSERT_TRUE(nxt.has_value()) << "decode ended early at i=" << i;
        EXPECT_EQ(nxt->first, series[i].first) << "timestamp mismatch at i=" << i;
        EXPECT_NEAR(nxt->second, series[i].second, 1e-12) << "value mismatch at i=" << i;
    }
    auto extra = dec.next();
    EXPECT_FALSE(extra.has_value()) << "decoder produced extra values";
}
