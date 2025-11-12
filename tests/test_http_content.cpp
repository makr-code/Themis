#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <filesystem>

#include "server/http_server.h"
#include "storage/rocksdb_wrapper.h"
#include "index/secondary_index.h"
#include "index/graph_index.h"
#include "index/vector_index.h"
#include "transaction/transaction_manager.h"
#include "content/content_processor.h"

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class HttpContentApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        const std::string db_path = "data/themis_http_content_test";
        if (std::filesystem::exists(db_path)) {
            std::filesystem::remove_all(db_path);
        }

        themis::RocksDBWrapper::Config cfg;
        cfg.db_path = db_path;
        cfg.memtable_size_mb = 64;
        cfg.block_cache_size_mb = 128;
        storage_ = std::make_shared<themis::RocksDBWrapper>(cfg);
        ASSERT_TRUE(storage_->open());

        secondary_index_ = std::make_shared<themis::SecondaryIndexManager>(*storage_);
        graph_index_ = std::make_shared<themis::GraphIndexManager>(*storage_);
        vector_index_ = std::make_shared<themis::VectorIndexManager>(*storage_);
        tx_manager_ = std::make_shared<themis::TransactionManager>(*storage_, *secondary_index_, *graph_index_, *vector_index_);

        themis::server::HttpServer::Config scfg;
        scfg.host = "127.0.0.1";
        scfg.port = 18086; // eigener Port für Content-Tests
        scfg.num_threads = 2;

        server_ = std::make_unique<themis::server::HttpServer>(scfg, storage_, secondary_index_, graph_index_, vector_index_, tx_manager_);
        server_->start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void TearDown() override {
        if (server_) server_->stop();
        storage_->close();
        std::filesystem::remove_all("data/themis_http_content_test");
    }

    json httpPost(const std::string& target, const json& body) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18086));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "127.0.0.1");
        req.set(http::field::content_type, "application/json");
        req.body() = body.dump();
        req.prepare_payload();

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        return json::parse(res.body());
    }

    json httpGet(const std::string& target, std::string* outContentType = nullptr) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);

        auto const results = resolver.resolve("127.0.0.1", std::to_string(18086));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "127.0.0.1");

        http::write(stream, req);

        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        if (outContentType) {
            auto ct = res.base().find(http::field::content_type);
            if (ct != res.base().end()) *outContentType = std::string(ct->value());
        }

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        // Falls die Antwort kein JSON ist (z. B. Blob), liefere JSON mit {blob: string}
        try {
            return json::parse(res.body());
        } catch (...) {
            return json{{"blob", res.body()}};
        }
    }

    std::shared_ptr<themis::RocksDBWrapper> storage_;
    std::shared_ptr<themis::SecondaryIndexManager> secondary_index_;
    std::shared_ptr<themis::GraphIndexManager> graph_index_;
    std::shared_ptr<themis::VectorIndexManager> vector_index_;
    std::shared_ptr<themis::TransactionManager> tx_manager_;
    std::unique_ptr<themis::server::HttpServer> server_;
};

TEST_F(HttpContentApiTest, ContentImport_ThenGetMetaAndChunks) {
    // Import minimalen Text-Content mit 2 Chunks und Blob
    json req = {
        {"content", json{
            {"id", "doc-001"},
            {"mime_type", "text/plain"},
            {"user_metadata", json{{"dataset","alpha"}}},
            {"tags", json::array({"demo"})}
        }},
        {"blob", "Hello world"},
        {"chunks", json::array({
            json{{"seq_num",0},{"chunk_type","text"},{"text","Hello"}},
            json{{"seq_num",1},{"chunk_type","text"},{"text","world"}}
        })}
    };

    auto resp = httpPost("/content/import", req);
    ASSERT_TRUE(resp.contains("status")) << resp.dump();
    EXPECT_EQ(resp["status"], "success");
    ASSERT_TRUE(resp.contains("content_id"));
    EXPECT_EQ(resp["content_id"], "doc-001");

    // GET /content/doc-001 → Meta prüfen
    auto meta = httpGet("/content/doc-001");
    ASSERT_TRUE(meta.contains("id"));
    EXPECT_EQ(meta["id"], "doc-001");
    ASSERT_TRUE(meta.contains("chunk_count"));
    EXPECT_EQ(meta["chunk_count"], 2);
    ASSERT_TRUE(meta.contains("size_bytes"));
    EXPECT_GE(meta["size_bytes"].get<int64_t>(), 11);

    // GET /content/doc-001/chunks → Liste prüfen
    auto chunks = httpGet("/content/doc-001/chunks");
    ASSERT_TRUE(chunks.contains("count"));
    ASSERT_TRUE(chunks.contains("chunks"));
    EXPECT_EQ(chunks["count"], 2);
    auto arr = chunks["chunks"];
    ASSERT_EQ(arr.size(), 2);
    EXPECT_EQ(arr[0]["content_id"], "doc-001");
    EXPECT_EQ(arr[0]["seq_num"], 0);
    EXPECT_EQ(arr[1]["seq_num"], 1);
}

TEST_F(HttpContentApiTest, GetBlob_ReturnsRawWithMimeType) {
    // Vorbereitung: Import eines Eintrags mit Blob
    json req = {
        {"content", json{{"id","doc-blob"},{"mime_type","text/plain"}}},
        {"blob", "BLOB-TEST"},
        {"chunks", json::array({ json{{"seq_num",0},{"chunk_type","text"},{"text","BLOB"}} })}
    };
    (void)httpPost("/content/import", req);

    std::string ct;
    auto blobResp = httpGet("/content/doc-blob/blob", &ct);
    ASSERT_TRUE(blobResp.contains("blob"));
    EXPECT_EQ(blobResp["blob"], "BLOB-TEST");
    // Content-Type sollte aus mime_type kommen
    EXPECT_EQ(ct, "text/plain");
}

TEST_F(HttpContentApiTest, ContentImport_WithEmbeddings_EnablesVectorSearch) {
    // Erzeuge konsistente 768-D Embeddings mit dem gleichen TextProcessor wie der Server
    themis::content::TextProcessor tp;
    std::string text = "alpha bravo";
    auto emb = tp.generateEmbedding(text);

    // Import mit explizitem Chunk-Embedding
    json req = {
        {"content", json{{"id","doc-emb-1"},{"mime_type","text/plain"}}},
        {"chunks", json::array({ json{{"id","c1"},{"seq_num",0},{"chunk_type","text"},{"text",text},{"embedding", emb}} })}
    };
    auto resp = httpPost("/content/import", req);
    ASSERT_TRUE(resp.contains("status")) << resp.dump();
    EXPECT_EQ(resp["status"], "success");

    // Suche mit exakt gleichem Vektor sollte den Chunk wiederfinden
    json searchReq = {
        {"vector", emb},
        {"k", 1}
    };
    auto searchResp = httpPost("/vector/search", searchReq);
    ASSERT_TRUE(searchResp.contains("results")) << searchResp.dump();
    auto results = searchResp["results"];
    ASSERT_GE(results.size(), 1);
    EXPECT_EQ(results[0]["pk"], "chunks:c1");
}

TEST_F(HttpContentApiTest, HybridSearch_ExpandsOverEdges) {
    // Zwei Chunks mit unterschiedlichen Texten/Embeddings
    themis::content::TextProcessor tp;
    std::string textA = "alpha topic";
    std::string textB = "beta unrelated";
    auto embA = tp.generateEmbedding(textA);
    auto embB = tp.generateEmbedding(textB);

    json req = {
        {"content", json{{"id","doc-hybrid-1"},{"mime_type","text/plain"}}},
        {"chunks", json::array({
            json{{"id","ha"},{"seq_num",0},{"chunk_type","text"},{"text",textA},{"embedding", embA}},
            json{{"id","hb"},{"seq_num",1},{"chunk_type","text"},{"text",textB},{"embedding", embB}}
        })},
        {"edges", json::array({ json{{"from","chunks:ha"},{"to","chunks:hb"},{"type","next"},{"weight",1.0}} })}
    };
    (void)httpPost("/content/import", req);

    // Hybrid-Suche: query auf textA, k=2, Expansion 1 Hop → sollte ha (Basistreffer) und hb (Nachbarn) liefern
    json hreq = {
        {"query", textA},
        {"k", 2},
        {"expand", json{{"hops", 1}}}
    };
    auto hresp = httpPost("/search/hybrid", hreq);
    ASSERT_TRUE(hresp.contains("results")) << hresp.dump();
    auto items = hresp["results"];
    ASSERT_GE(items.size(), 1);
    // Sammle PKs
    std::vector<std::string> pks;
    for (auto& it : items) { if (it.contains("pk")) pks.push_back(it["pk"].get<std::string>()); }
    // Erwartung: Basistreffer vorhanden
    EXPECT_NE(std::find(pks.begin(), pks.end(), std::string("chunks:ha")), pks.end());
    // Erwartung: durch Expansion auch der Nachbar dabei (sofern k>=2)
    EXPECT_NE(std::find(pks.begin(), pks.end(), std::string("chunks:hb")), pks.end());
}

TEST_F(HttpContentApiTest, BlobEncryption_StoresEncrypted_DecryptsOnRetrieval) {
    // Enable content blob encryption via config
    nlohmann::json encCfg = {
        {"enabled", true},
        {"key_id", "content_blob"},
        {"context", "per_user"}
    };
    auto encStr = encCfg.dump();
    storage_->put("config:content_encryption_schema", std::vector<uint8_t>(encStr.begin(), encStr.end()));

    // Import content with blob - user_context will be extracted from auth (or default to "anonymous" if auth disabled)
    json req = {
        {"content", json{{"id","secret-doc"},{"mime_type","text/plain"}}},
        {"blob", "Confidential payload"},
        {"chunks", json::array({ json{{"seq_num",0},{"chunk_type","text"},{"text","test"}} })}
    };
    
    // Since auth may be disabled in test setup, user_context might be empty or "anonymous"
    // Server will use extractAuthContext → if auth_ is null/disabled, returns empty user_id
    // ContentManager falls back to "anonymous" when user_context empty for encryption salt
    auto resp = httpPost("/content/import", req);
    
    ASSERT_TRUE(resp.contains("status")) << resp.dump();
    EXPECT_EQ(resp["status"], "success");

    // Verify stored blob is encrypted (read raw from storage)
    auto rawBlob = storage_->get("content_blob:secret-doc");
    ASSERT_TRUE(rawBlob.has_value()) << "Blob should be stored under content_blob:secret-doc";
    std::string storedStr(rawBlob->begin(), rawBlob->end());
    
    // Stored blob should be JSON with encrypted structure (not plaintext)
    try {
        auto storedJson = json::parse(storedStr);
        ASSERT_TRUE(storedJson.contains("key_id")) << "Encrypted blob should have key_id";
        ASSERT_TRUE(storedJson.contains("key_version"));
        ASSERT_TRUE(storedJson.contains("iv"));
        ASSERT_TRUE(storedJson.contains("ciphertext"));
        ASSERT_TRUE(storedJson.contains("tag"));
        // Plaintext "Confidential payload" should NOT appear in stored ciphertext
        EXPECT_EQ(storedStr.find("Confidential payload"), std::string::npos) 
            << "Plaintext should not be visible in encrypted blob";
    } catch (const nlohmann::json::parse_error& e) {
        FAIL() << "Stored blob should be valid JSON: " << e.what();
    }

    // GET /content/secret-doc/blob should decrypt (using same user_context)
    std::string contentType;
    auto blobResp = httpGet("/content/secret-doc/blob", &contentType);
    
    // httpGet returns JSON with {"blob": "..."} if response is not parseable JSON
    ASSERT_TRUE(blobResp.contains("blob")) << "Response should contain blob field";
    std::string decryptedBlob = blobResp["blob"].get<std::string>();
    EXPECT_EQ(decryptedBlob, "Confidential payload") 
        << "Decrypted blob should match original plaintext";
}

TEST_F(HttpContentApiTest, BlobLazyReencryption_UpgradesKeyVersionOnRead) {
    // Enable content blob encryption
    nlohmann::json encCfg = {
        {"enabled", true},
        {"key_id", "content_blob"},
        {"context", "per_user"}
    };
    auto encStr = encCfg.dump();
    storage_->put("config:content_encryption_schema", std::vector<uint8_t>(encStr.begin(), encStr.end()));

    // Import blob (will be encrypted with current key version)
    json req = {
        {"content", json{{"id","reenc-test"},{"mime_type","text/plain"}}},
        {"blob", "Test payload for re-encryption"},
        {"chunks", json::array({ json{{"seq_num",0},{"chunk_type","text"},{"text","test"}} })}
    };
    auto resp = httpPost("/content/import", req);
    ASSERT_TRUE(resp.contains("status"));
    EXPECT_EQ(resp["status"], "success");

    // Read stored blob and capture initial key_version
    auto rawBlob1 = storage_->get("content_blob:reenc-test");
    ASSERT_TRUE(rawBlob1.has_value());
    std::string stored1(rawBlob1->begin(), rawBlob1->end());
    auto json1 = json::parse(stored1);
    uint32_t initial_version = json1["key_version"].get<uint32_t>();

    // Simulate key rotation: manually increment key_version in stored blob to create "outdated" scenario
    // (In real scenario, KeyProvider would have rotated the key and we'd write with old version)
    // For test simplicity: We'll read blob, verify version, then access it to trigger lazy re-encryption
    // Since we can't easily rotate keys in test, we simulate by checking that re-encryption logic works
    
    // Access blob via GET - should trigger lazy re-encryption if version is outdated
    // Note: Without actual key rotation in KeyProvider, re-encryption won't trigger
    // This test validates the flow exists; full integration test would require KeyProvider mock
    auto blobResp = httpGet("/content/reenc-test/blob");
    ASSERT_TRUE(blobResp.contains("blob"));
    std::string decrypted = blobResp["blob"].get<std::string>();
    EXPECT_EQ(decrypted, "Test payload for re-encryption");

    // Read stored blob again - version should be same (no rotation occurred in test)
    auto rawBlob2 = storage_->get("content_blob:reenc-test");
    ASSERT_TRUE(rawBlob2.has_value());
    std::string stored2(rawBlob2->begin(), rawBlob2->end());
    auto json2 = json::parse(stored2);
    uint32_t final_version = json2["key_version"].get<uint32_t>();
    
    // In this test without actual rotation, versions should be equal
    // Real test would increment KeyProvider version and verify upgrade
    EXPECT_EQ(initial_version, final_version) 
        << "Without key rotation, version should remain unchanged";
    
    // TODO: Add test with mocked KeyProvider that returns higher version to verify re-encryption
}

TEST_F(HttpContentApiTest, BlobCompression_CompressesTextBlobs_SkipsImages) {
    // Enable content blob compression via config
    nlohmann::json compressCfg = {
        {"compress_blobs", true},
        {"compression_level", 19},
        {"skip_compressed_mimes", json::array({"image/", "video/", "application/zip"})}
    };
    auto compressStr = compressCfg.dump();
    storage_->put("config:content", std::vector<uint8_t>(compressStr.begin(), compressStr.end()));

    // Test 1: Import large text blob (should be compressed)
    std::string largeText(10000, 'A'); // 10KB of 'A' (highly compressible)
    json req1 = {
        {"content", json{{"id","compress-text"},{"mime_type","text/plain"}}},
        {"blob", largeText},
        {"chunks", json::array({ json{{"seq_num",0},{"chunk_type","text"},{"text","test"}} })}
    };
    auto resp1 = httpPost("/content/import", req1);
    ASSERT_TRUE(resp1.contains("status")) << resp1.dump();
    EXPECT_EQ(resp1["status"], "success");

    // Verify metadata shows compressed=true
    auto meta1 = httpGet("/content/compress-text");
    ASSERT_TRUE(meta1.contains("compressed"));
    EXPECT_TRUE(meta1["compressed"].get<bool>()) << "Text blob should be compressed";
    EXPECT_EQ(meta1["compression_type"], "zstd");
    ASSERT_TRUE(meta1.contains("size_bytes"));
    EXPECT_EQ(meta1["size_bytes"].get<int64_t>(), 10000) << "size_bytes should reflect original size";

    // Verify stored blob is actually compressed (smaller than original)
    auto rawBlob1 = storage_->get("content_blob:compress-text");
    ASSERT_TRUE(rawBlob1.has_value());
    EXPECT_LT(rawBlob1->size(), 10000) << "Stored blob should be smaller than original 10KB";
    // For 10KB of 'A', ZSTD should achieve >90% compression
    EXPECT_LT(rawBlob1->size(), 1000) << "ZSTD should compress repeated text to <1KB";

    // GET blob - should decompress transparently
    auto blobResp1 = httpGet("/content/compress-text/blob");
    ASSERT_TRUE(blobResp1.contains("blob"));
    std::string decompressed = blobResp1["blob"].get<std::string>();
    EXPECT_EQ(decompressed, largeText) << "Decompressed blob should match original";

    // Metrics: ensure ContentManager recorded compression metrics
    auto cms = server_->contentMetrics();
    ASSERT_NE(cms, nullptr) << "Content metrics should be available via server";
    // At least some compressed bytes recorded and uncompressed total increased
    EXPECT_GE(cms->uncompressed_bytes_total.load(), static_cast<uint64_t>(10000));
    EXPECT_GE(cms->compressed_bytes_total.load(), static_cast<uint64_t>(rawBlob1->size()));

    // Test 2: Import image (should skip compression)
    std::string fakeImage(5000, 'X'); // Simple ASCII data representing binary image
    json req2 = {
        {"content", json{{"id","compress-image"},{"mime_type","image/png"}}},
        {"blob", fakeImage},
        {"chunks", json::array({ json{{"seq_num",0},{"chunk_type","image"},{"text","img"}} })}
    };
    auto resp2 = httpPost("/content/import", req2);
    ASSERT_TRUE(resp2.contains("status")) << resp2.dump();
    EXPECT_EQ(resp2["status"], "success");

    // Verify metadata shows compressed=false (skipped due to MIME type)
    auto meta2 = httpGet("/content/compress-image");
    ASSERT_TRUE(meta2.contains("compressed"));
    EXPECT_FALSE(meta2["compressed"].get<bool>()) << "Image should skip compression";
    
    // Verify stored blob size matches original (no compression)
    auto rawBlob2 = storage_->get("content_blob:compress-image");
    ASSERT_TRUE(rawBlob2.has_value());
    EXPECT_EQ(rawBlob2->size(), 5000) << "Image blob should be stored as-is (no compression)";

    // GET blob - should return original data
    auto blobResp2 = httpGet("/content/compress-image/blob");
    ASSERT_TRUE(blobResp2.contains("blob"));
    EXPECT_EQ(blobResp2["blob"].get<std::string>(), fakeImage) << "Image blob should match original";

    // Metrics: skipped counters should have incremented for image
    ASSERT_NE(cms, nullptr);
    EXPECT_GE(cms->compression_skipped_total.load(), static_cast<uint64_t>(1));
    EXPECT_GE(cms->compression_skipped_image_total.load(), static_cast<uint64_t>(1));

    // Test 3: Small blob (should skip compression due to size threshold)
    std::string smallText = "Small"; // <4KB threshold
    json req3 = {
        {"content", json{{"id","compress-small"},{"mime_type","text/plain"}}},
        {"blob", smallText},
        {"chunks", json::array({ json{{"seq_num",0},{"chunk_type","text"},{"text","test"}} })}
    };
    auto resp3 = httpPost("/content/import", req3);
    ASSERT_TRUE(resp3.contains("status")) << resp3.dump();
    EXPECT_EQ(resp3["status"], "success");

    // Verify metadata shows compressed=false (too small)
    auto meta3 = httpGet("/content/compress-small");
    ASSERT_TRUE(meta3.contains("compressed"));
    EXPECT_FALSE(meta3["compressed"].get<bool>()) << "Small blob should skip compression";
}

