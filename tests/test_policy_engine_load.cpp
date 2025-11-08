#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include "server/policy_engine.h"

using themis::PolicyEngine;

class PolicyEngineLoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories("config");
    }

    void TearDown() override {
        // keep file for other tests if needed; no cleanup
    }
};

TEST_F(PolicyEngineLoadTest, LoadYamlSequence_SucceedsAndListsPolicies) {
    // Write a simple YAML sequence with two policies
    const char* yaml = R"YAML(
- id: allow-metrics-readonly
  name: readonly darf /metrics
  subjects: ["readonly"]
  actions: ["metrics.read"]
  resources: ["/metrics"]
  effect: allow

- id: allow-admin-policies-export
  name: admin darf Policies exportieren
  subjects: ["admin"]
  actions: ["admin"]
  resources: ["/policies/export/ranger"]
  effect: allow
)YAML";

    std::ofstream pf("config/policies.yaml", std::ios::binary);
    ASSERT_TRUE(static_cast<bool>(pf));
    pf << yaml;
    pf.close();

    PolicyEngine pe;
    std::string err;
    ASSERT_TRUE(pe.loadFromFile("config/policies.yaml", &err)) << err;

    auto list = pe.listPolicies();
    ASSERT_GE(list.size(), 2u);

    // find admin export policy
    bool found_admin = false;
    for (const auto& p : list) {
        if (p.id == "allow-admin-policies-export") {
            found_admin = true;
            // expect admin action and resource prefix
            ASSERT_TRUE(p.actions.count("admin") > 0);
            ASSERT_FALSE(p.resources.empty());
            break;
        }
    }
    EXPECT_TRUE(found_admin);
}
