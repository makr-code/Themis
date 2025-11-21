// Benchmark: Policy Rule Evaluation Performance
// Tests evaluation of simple rules, complex nested rules, and large policy sets

#include <benchmark/benchmark.h>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

// Mock policy engine
class PolicyEngine {
public:
    enum class Effect { ALLOW, DENY };
    enum class PolicyType { RBAC, ABAC };
    
    struct Rule {
        std::string name;
        std::function<bool(const std::map<std::string, std::string>&)> condition;
        Effect effect;
    };
    
    struct Policy {
        std::string id;
        std::vector<Rule> rules;
        PolicyType type;
    };
    
    struct EvaluationContext {
        std::string user;
        std::string resource;
        std::string action;
        std::map<std::string, std::string> attributes;
    };
    
    Effect evaluate_rule(const Rule& rule, const EvaluationContext& ctx) {
        if (rule.condition(ctx.attributes)) {
            return rule.effect;
        }
        return Effect::DENY;
    }
    
    Effect evaluate_policy(const Policy& policy, const EvaluationContext& ctx) {
        for (const auto& rule : policy.rules) {
            if (evaluate_rule(rule, ctx) == Effect::ALLOW) {
                return Effect::ALLOW;
            }
        }
        return Effect::DENY;
    }
    
    Effect evaluate_policy_set(const std::vector<Policy>& policies, const EvaluationContext& ctx) {
        for (const auto& policy : policies) {
            if (evaluate_policy(policy, ctx) == Effect::ALLOW) {
                return Effect::ALLOW;
            }
        }
        return Effect::DENY;
    }
    
    bool has_conflict(const std::vector<Policy>& policies, const EvaluationContext& ctx) {
        int allow_count = 0;
        int deny_count = 0;
        
        for (const auto& policy : policies) {
            Effect effect = evaluate_policy(policy, ctx);
            if (effect == Effect::ALLOW) allow_count++;
            else deny_count++;
        }
        
        return allow_count > 0 && deny_count > 0;
    }
    
    void add_policy(const Policy& policy) {
        policies_.push_back(policy);
    }
    
    void enable_cache() { cache_enabled_ = true; }
    void disable_cache() { cache_enabled_ = false; }
    
private:
    std::vector<Policy> policies_;
    bool cache_enabled_ = false;
    std::map<std::string, Effect> cache_;
};

// Create simple RBAC policy
PolicyEngine::Policy create_simple_rbac_policy() {
    PolicyEngine::Policy policy;
    policy.id = "simple_rbac";
    policy.type = PolicyEngine::PolicyType::RBAC;
    
    PolicyEngine::Rule rule;
    rule.name = "admin_all_access";
    rule.condition = [](const std::map<std::string, std::string>& attrs) {
        auto it = attrs.find("role");
        return it != attrs.end() && it->second == "admin";
    };
    rule.effect = PolicyEngine::Effect::ALLOW;
    
    policy.rules.push_back(rule);
    return policy;
}

// Create complex nested ABAC policy
PolicyEngine::Policy create_complex_abac_policy() {
    PolicyEngine::Policy policy;
    policy.id = "complex_abac";
    policy.type = PolicyEngine::PolicyType::ABAC;
    
    PolicyEngine::Rule rule;
    rule.name = "multi_condition_access";
    rule.condition = [](const std::map<std::string, std::string>& attrs) {
        auto role = attrs.find("role");
        auto dept = attrs.find("department");
        auto time = attrs.find("time_of_day");
        
        return role != attrs.end() && role->second == "manager" &&
               dept != attrs.end() && dept->second == "engineering" &&
               time != attrs.end() && time->second == "business_hours";
    };
    rule.effect = PolicyEngine::Effect::ALLOW;
    
    policy.rules.push_back(rule);
    return policy;
}

// Benchmark: Simple rule evaluation
static void BM_SimpleRuleEvaluation(benchmark::State& state) {
    PolicyEngine engine;
    PolicyEngine::Policy policy = create_simple_rbac_policy();
    
    PolicyEngine::EvaluationContext ctx;
    ctx.user = "alice";
    ctx.resource = "/api/data";
    ctx.action = "read";
    ctx.attributes["role"] = "admin";
    
    for (auto _ : state) {
        auto effect = engine.evaluate_policy(policy, ctx);
        benchmark::DoNotOptimize(effect);
    }
}
BENCHMARK(BM_SimpleRuleEvaluation);

// Benchmark: Complex nested rules
static void BM_ComplexRuleEvaluation(benchmark::State& state) {
    PolicyEngine engine;
    PolicyEngine::Policy policy = create_complex_abac_policy();
    
    PolicyEngine::EvaluationContext ctx;
    ctx.user = "bob";
    ctx.resource = "/api/sensitive";
    ctx.action = "write";
    ctx.attributes["role"] = "manager";
    ctx.attributes["department"] = "engineering";
    ctx.attributes["time_of_day"] = "business_hours";
    
    for (auto _ : state) {
        auto effect = engine.evaluate_policy(policy, ctx);
        benchmark::DoNotOptimize(effect);
    }
}
BENCHMARK(BM_ComplexRuleEvaluation);

// Benchmark: Policy set evaluation (varying number of policies)
static void BM_PolicySetEvaluation(benchmark::State& state) {
    size_t num_policies = state.range(0);
    std::vector<PolicyEngine::Policy> policies;
    
    for (size_t i = 0; i < num_policies; ++i) {
        policies.push_back(create_simple_rbac_policy());
    }
    
    PolicyEngine engine;
    PolicyEngine::EvaluationContext ctx;
    ctx.user = "charlie";
    ctx.attributes["role"] = "admin";
    
    for (auto _ : state) {
        auto effect = engine.evaluate_policy_set(policies, ctx);
        benchmark::DoNotOptimize(effect);
    }
    
    state.counters["policies"] = num_policies;
}
BENCHMARK(BM_PolicySetEvaluation)->Arg(10)->Arg(100)->Arg(1000);

// Benchmark: Rule caching impact
static void BM_CachingImpact(benchmark::State& state) {
    bool use_cache = state.range(0);
    
    PolicyEngine engine;
    if (use_cache) engine.enable_cache();
    else engine.disable_cache();
    
    PolicyEngine::Policy policy = create_simple_rbac_policy();
    PolicyEngine::EvaluationContext ctx;
    ctx.attributes["role"] = "admin";
    
    for (auto _ : state) {
        auto effect = engine.evaluate_policy(policy, ctx);
        benchmark::DoNotOptimize(effect);
    }
    
    state.SetLabel(use_cache ? "with_cache" : "no_cache");
}
BENCHMARK(BM_CachingImpact)->Arg(0)->Arg(1);

// Benchmark: RBAC vs ABAC comparison
static void BM_RBACvsABAC(benchmark::State& state) {
    bool use_rbac = state.range(0);
    
    PolicyEngine engine;
    PolicyEngine::Policy policy = use_rbac ? 
        create_simple_rbac_policy() : 
        create_complex_abac_policy();
    
    PolicyEngine::EvaluationContext ctx;
    ctx.attributes["role"] = use_rbac ? "admin" : "manager";
    if (!use_rbac) {
        ctx.attributes["department"] = "engineering";
        ctx.attributes["time_of_day"] = "business_hours";
    }
    
    for (auto _ : state) {
        auto effect = engine.evaluate_policy(policy, ctx);
        benchmark::DoNotOptimize(effect);
    }
    
    state.SetLabel(use_rbac ? "RBAC" : "ABAC");
}
BENCHMARK(BM_RBACvsABAC)->Arg(1)->Arg(0);

// Benchmark: Policy conflict detection
static void BM_ConflictDetection(benchmark::State& state) {
    size_t num_policies = state.range(0);
    PolicyEngine engine;
    
    std::vector<PolicyEngine::Policy> policies;
    for (size_t i = 0; i < num_policies; ++i) {
        policies.push_back(create_simple_rbac_policy());
    }
    
    PolicyEngine::EvaluationContext ctx;
    ctx.attributes["role"] = "admin";
    
    for (auto _ : state) {
        bool conflict = engine.has_conflict(policies, ctx);
        benchmark::DoNotOptimize(conflict);
    }
}
BENCHMARK(BM_ConflictDetection)->Arg(10)->Arg(50)->Arg(100);

// Benchmark: Evaluation throughput
static void BM_EvaluationThroughput(benchmark::State& state) {
    PolicyEngine engine;
    PolicyEngine::Policy policy = create_simple_rbac_policy();
    
    PolicyEngine::EvaluationContext ctx;
    ctx.attributes["role"] = "admin";
    
    size_t evaluations = 0;
    for (auto _ : state) {
        auto effect = engine.evaluate_policy(policy, ctx);
        benchmark::DoNotOptimize(effect);
        evaluations++;
    }
    
    state.counters["evaluations/sec"] = benchmark::Counter(
        evaluations,
        benchmark::Counter::kIsRate
    );
}
BENCHMARK(BM_EvaluationThroughput);

BENCHMARK_MAIN();
