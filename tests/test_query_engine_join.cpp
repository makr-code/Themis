#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "storage/rocksdb_wrapper.h"
#include "storage/base_entity.h"
#include "index/secondary_index.h"
#include "query/query_engine.h"
#include "query/aql_parser.h"
#include "query/aql_translator.h"

using namespace themis;
using namespace themis::query;

namespace {
std::string makeTempPath(const std::string& suffix) {
    namespace fs = std::filesystem;
    auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    return (fs::temp_directory_path() / ("themis_qe_join_" + suffix + std::to_string(now))).string();
}

void putEntity(SecondaryIndexManager& idx, const std::string& table, const std::string& pk, BaseEntity::FieldMap fields) {
    BaseEntity entity = BaseEntity::fromFields(pk, fields);
    ASSERT_TRUE(idx.put(table, entity).ok);
}
}

TEST(QueryEngineJoinLetTest, SingleFor_LetFilterEvaluatedAfterBinding) {
    RocksDBWrapper::Config cfg; cfg.db_path = makeTempPath("let_");
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);

    putEntity(idx, "users", "u1", {{"name", std::string("Alice")}, {"city", std::string("Berlin")}});
    putEntity(idx, "users", "u2", {{"name", std::string("Bob")}, {"city", std::string("Hamburg")}});

    AQLParser parser;
    auto parse = parser.parse("FOR u IN users LET c = u.city FILTER c == \"Berlin\" RETURN u");
    ASSERT_TRUE(parse.success) << parse.error.message;

    auto translate = AQLTranslator::translate(parse.query);
    ASSERT_TRUE(translate.success);
    ASSERT_TRUE(translate.join.has_value());

    QueryEngine engine(db, idx);
    const auto& jq = *translate.join;
    auto [status, rows] = engine.executeJoin(jq.for_nodes, jq.filters, jq.let_nodes, jq.return_node, jq.sort, jq.limit);

    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_TRUE(rows[0].is_object());
    EXPECT_EQ(rows[0]["name"].get<std::string>(), "Alice");

    db.close();
    std::filesystem::remove_all(cfg.db_path);
}

TEST(QueryEngineJoinLetTest, DoubleFor_LetFiltersUseDerivedValues) {
    RocksDBWrapper::Config cfg; cfg.db_path = makeTempPath("join_");
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);

    putEntity(idx, "users", "u1", {{"name", std::string("Alice")}});
    putEntity(idx, "users", "u2", {{"name", std::string("Bob")}});

    putEntity(idx, "orders", "o1", {{"user_id", std::string("u1")}, {"amount", int64_t(5)}});
    putEntity(idx, "orders", "o2", {{"user_id", std::string("u1")}, {"amount", int64_t(15)}});
    putEntity(idx, "orders", "o3", {{"user_id", std::string("u2")}, {"amount", int64_t(25)}});

    AQLParser parser;
    auto parse = parser.parse("FOR u IN users FOR o IN orders LET total = o.amount FILTER u._key == o.user_id FILTER total > 10 RETURN {user: u.name, amount: total}");
    ASSERT_TRUE(parse.success) << parse.error.message;

    auto translate = AQLTranslator::translate(parse.query);
    ASSERT_TRUE(translate.success);
    ASSERT_TRUE(translate.join.has_value());

    QueryEngine engine(db, idx);
    const auto& jq = *translate.join;
    auto [status, rows] = engine.executeJoin(jq.for_nodes, jq.filters, jq.let_nodes, jq.return_node, jq.sort, jq.limit);

    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_EQ(rows.size(), 2u); // orders o2 and o3 survive amount filter

    std::vector<std::string> users;
    for (const auto& row : rows) {
        ASSERT_TRUE(row.is_object());
        users.push_back(row["user"].get<std::string>());
        EXPECT_GE(row["amount"].get<int64_t>(), 15);
    }
    std::sort(users.begin(), users.end());
    EXPECT_EQ(users, (std::vector<std::string>{"Alice", "Bob"}));

    db.close();
    std::filesystem::remove_all(cfg.db_path);
}

TEST(QueryEngineJoinLetTest, ReturnDistinctRemovesDuplicateJoinRows) {
    RocksDBWrapper::Config cfg; cfg.db_path = makeTempPath("distinct_");
    RocksDBWrapper db(cfg);
    ASSERT_TRUE(db.open());
    SecondaryIndexManager idx(db);

    putEntity(idx, "users", "u1", {{"name", std::string("Alice")}});
    putEntity(idx, "users", "u2", {{"name", std::string("Bob")}});

    // Two orders for Alice, one for Bob -> duplicate join rows when returning user name
    putEntity(idx, "orders", "o1", {{"user_id", std::string("u1")}});
    putEntity(idx, "orders", "o2", {{"user_id", std::string("u1")}});
    putEntity(idx, "orders", "o3", {{"user_id", std::string("u2")}});

    AQLParser parser;
    auto parse = parser.parse("FOR u IN users FOR o IN orders FILTER u._key == o.user_id RETURN DISTINCT u.name");
    ASSERT_TRUE(parse.success) << parse.error.message;

    auto translate = AQLTranslator::translate(parse.query);
    ASSERT_TRUE(translate.success);
    ASSERT_TRUE(translate.join.has_value());

    QueryEngine engine(db, idx);
    const auto& jq = *translate.join;
    auto [status, rows] = engine.executeJoin(jq.for_nodes, jq.filters, jq.let_nodes, jq.return_node, jq.sort, jq.limit);

    ASSERT_TRUE(status.ok) << status.message;
    ASSERT_EQ(rows.size(), 2u);
    std::vector<std::string> names;
    for (const auto& row : rows) {
        ASSERT_TRUE(row.is_string());
        names.push_back(row.get<std::string>());
    }
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names, (std::vector<std::string>{"Alice", "Bob"}));

    db.close();
    std::filesystem::remove_all(cfg.db_path);
}
