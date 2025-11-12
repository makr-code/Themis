#include <gtest/gtest.h>
#include "security/signing.h"

using namespace themis;

TEST(SigningService, MockSignVerifyRoundtrip) {
    auto svc = createMockSigningService();
    std::string msg = "Hello, Themis PKI";
    std::vector<uint8_t> data(msg.begin(), msg.end());
    auto res = svc->sign(data, "test-key");
    ASSERT_FALSE(res.signature.empty());
    EXPECT_TRUE(svc->verify(data, res.signature, "test-key"));
}
