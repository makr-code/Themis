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
