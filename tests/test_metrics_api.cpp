#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <string>
#include <filesystem>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class MetricsServerFixture {
public:
    MetricsServerFixture() : server_running_(false), server_port_(8765) {}
    ~MetricsServerFixture() { stopServer(); }

    void startServer() {
        if (server_running_) return;
#ifdef _WIN32
        // Try to reuse existing server if already running
        try {
            if (check("/health") == http::status::ok) { server_running_ = true; return; }
        } catch (...) {}
        // Start detached process
    STARTUPINFOW si{}; PROCESS_INFORMATION pi{}; si.cb = sizeof(si);
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    std::filesystem::path appPath = exeDir / L"themis_server.exe";
    std::wstring app = appPath.wstring();
    std::filesystem::path rootDir = exeDir.parent_path().parent_path();
    std::wstring workDir = rootDir.wstring();
    BOOL ok = CreateProcessW(app.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
        if (!ok) { throw std::runtime_error("Failed to start server process (CreateProcessW)"); }
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
#else
        std::system("nohup ./build/Release/themis_server > /dev/null 2>&1 &");
#endif
        server_running_ = true;
        // wait for health
        bool ready=false;
        for (int i=0;i<50;++i){ std::this_thread::sleep_for(std::chrono::milliseconds(200)); if (check("/health") == http::status::ok) { ready=true; break; } }
        if (!ready) throw std::runtime_error("Server did not become ready within timeout");
    }

    void stopServer() {
        if (!server_running_) return;
#ifdef _WIN32
        std::system("powershell -NoProfile -Command \"Get-Process themis_server -ErrorAction SilentlyContinue | Stop-Process -Force\"");
#else
        std::system("pkill -9 themis_server");
#endif
        server_running_ = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    http::status check(const std::string& path) { return get(path).result(); }

    http::response<http::string_body> get(const std::string& target) {
        net::io_context ioc; tcp::resolver resolver(ioc); beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("localhost", std::to_string(server_port_));
        stream.connect(results);
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, "vccdb_test");
        http::write(stream, req);
        beast::flat_buffer buffer; http::response<http::string_body> res; http::read(stream, buffer, res);
        beast::error_code ec; stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res;
    }
private:
    std::atomic<bool> server_running_;
    uint16_t server_port_;
};

class MetricsApiTest : public ::testing::Test {
protected:
    void SetUp() override { server_ = std::make_unique<MetricsServerFixture>(); server_->startServer(); }
    void TearDown() override { server_->stopServer(); server_.reset(); }
    std::unique_ptr<MetricsServerFixture> server_;
};

TEST_F(MetricsApiTest, MetricsEndpoint_ExposesBasicCounters) {
    auto res = server_->get("/metrics");
    ASSERT_EQ(res.result(), http::status::ok);
    auto body = res.body();
    // basic counters and gauges
    EXPECT_NE(body.find("process_uptime_seconds"), std::string::npos);
    EXPECT_NE(body.find("vccdb_requests_total"), std::string::npos);
    EXPECT_NE(body.find("vccdb_errors_total"), std::string::npos);
    EXPECT_NE(body.find("vccdb_qps"), std::string::npos);
    EXPECT_NE(body.find("rocksdb_block_cache_usage_bytes"), std::string::npos);
    EXPECT_NE(body.find("rocksdb_block_cache_capacity_bytes"), std::string::npos);
}

TEST_F(MetricsApiTest, LatencyHistogram_ExportsBucketsAndSumCount) {
    // Generate a couple of requests to populate buckets
    for (int i=0;i<5;++i) {
        auto h = server_->get("/health");
        ASSERT_EQ(h.result(), http::status::ok);
    }
    auto res = server_->get("/metrics");
    ASSERT_EQ(res.result(), http::status::ok);
    auto body = res.body();
    // buckets
    EXPECT_NE(body.find("vccdb_latency_bucket_microseconds{le=\"100\"}"), std::string::npos);
    EXPECT_NE(body.find("vccdb_latency_bucket_microseconds{le=\"500\"}"), std::string::npos);
    EXPECT_NE(body.find("vccdb_latency_bucket_microseconds{le=\"1000\"}"), std::string::npos);
    EXPECT_NE(body.find("vccdb_latency_bucket_microseconds{le=\"+Inf\"}"), std::string::npos);
    // sum and count
    EXPECT_NE(body.find("vccdb_latency_sum_microseconds"), std::string::npos);
    EXPECT_NE(body.find("vccdb_latency_count"), std::string::npos);
}

TEST_F(MetricsApiTest, RocksDBMetrics_ExposePendingCompaction) {
    auto res = server_->get("/metrics");
    ASSERT_EQ(res.result(), http::status::ok);
    auto body = res.body();
    EXPECT_NE(body.find("rocksdb_pending_compaction_bytes"), std::string::npos);
}
