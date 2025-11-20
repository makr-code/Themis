#include <gtest/gtest.h>
#include "storage/rocksdb_wrapper.h"
#include "content/content_fs.h"
#include <random>
#include <filesystem>

using namespace themis;

namespace {
std::vector<uint8_t> makeData(size_t n) {
    std::vector<uint8_t> v(n);
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> dist(0,255);
    for (size_t i=0;i<n;++i) v[i] = static_cast<uint8_t>(dist(rng));
    return v;
}
}

class ContentFSTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(test_dir);
        RocksDBWrapper::Config cfg; cfg.db_path = test_dir; cfg.create_if_missing = true;
        db = std::make_unique<RocksDBWrapper>(cfg);
        ASSERT_TRUE(db->open());
        cfs = std::make_unique<ContentFS>(*db);
    }
    void TearDown() override {
        cfs.reset(); db.reset();
        std::filesystem::remove_all(test_dir);
    }
    std::string test_dir = "./test_content_fs_tmp";
    std::unique_ptr<RocksDBWrapper> db;
    std::unique_ptr<ContentFS> cfs;
};

TEST_F(ContentFSTest, PutGetRoundtrip) {
    auto data = makeData(64 * 1024);
    auto hex = ContentFS::sha256Hex(data);
    auto st = cfs->put("blob1", data, "application/octet-stream", hex);
    ASSERT_TRUE(st.ok) << st.message;

    auto [hst, meta] = cfs->head("blob1");
    ASSERT_TRUE(hst.ok) << hst.message;
    EXPECT_EQ(meta.pk, "blob1");
    EXPECT_EQ(meta.mime, "application/octet-stream");
    EXPECT_EQ(meta.size, data.size());
    EXPECT_EQ(meta.sha256_hex, hex);

    auto [gst, out] = cfs->get("blob1");
    ASSERT_TRUE(gst.ok) << gst.message;
    EXPECT_EQ(out, data);
}

TEST_F(ContentFSTest, RangeReads) {
    auto data = makeData(10000);
    ASSERT_TRUE(cfs->put("r1", data, "binary").ok);
    // First 100 bytes
    auto [st1, a] = cfs->getRange("r1", 0, 100);
    ASSERT_TRUE(st1.ok); ASSERT_EQ(a.size(), 100u); EXPECT_TRUE(std::equal(a.begin(), a.end(), data.begin()));
    // Middle 200 bytes
    auto [st2, b] = cfs->getRange("r1", 500, 200);
    ASSERT_TRUE(st2.ok); ASSERT_EQ(b.size(), 200u);
    EXPECT_TRUE(std::equal(b.begin(), b.end(), data.begin()+500));
    // To end
    auto [st3, c] = cfs->getRange("r1", 9800, 0);
    ASSERT_TRUE(st3.ok); ASSERT_EQ(c.size(), 200u);
    EXPECT_TRUE(std::equal(c.begin(), c.end(), data.begin()+9800));
}

TEST_F(ContentFSTest, ChecksumMismatch) {
    auto data = makeData(1024);
    auto st = cfs->put("bad", data, "bin", std::string("deadbeef"));
    ASSERT_FALSE(st.ok);
}

TEST_F(ContentFSTest, DeleteBlob) {
    auto data = makeData(4096);
    ASSERT_TRUE(cfs->put("x", data, "bin").ok);
    ASSERT_TRUE(cfs->remove("x").ok);
    auto [st, out] = cfs->get("x");
    ASSERT_FALSE(st.ok);
}
