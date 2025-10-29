#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

// Helper class to start/stop server for integration tests
class ServerFixture {
public:
    ServerFixture() : server_running_(false), server_port_(8765) {}
    
    ~ServerFixture() {
        stopServer();
    }
    
    void startServer() {
        if (server_running_) return;
        // If there's already a server running (manually started), reuse it
        try {
            if (checkServerHealth()) {
                std::ofstream log("tests\\server_start.log", std::ios::app);
                log << "startServer: detected existing server, reusing" << std::endl;
                log.close();
                server_running_ = true;
                return;
            }
        } catch (...) {
            // ignore and continue to start
        }
        
        // Start server in background process
        std::ofstream log("tests\\server_start.log", std::ios::app);
        log << "startServer: invoked" << std::endl;
    #ifdef _WIN32
    log << "startServer: attempting CreateProcessW" << std::endl;
    // Windows: starte Server relativ zum Test-Binary-Verzeichnis
    #include <windows.h>
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    std::filesystem::path appPath = exeDir / L"themis_server.exe";
    std::wstring app = appPath.wstring();
    // Use repository root as working directory (two levels up from exe dir)
    std::filesystem::path rootDir = exeDir.parent_path().parent_path();
    std::wstring workDir = rootDir.wstring();
    BOOL ok = CreateProcessW(app.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
        if (!ok) {
            DWORD err = GetLastError();
            log << "CreateProcessW failed, GetLastError=" << err << std::endl;
            log.close();
            throw std::runtime_error("Failed to start server process (CreateProcess failed)");
        }
        log << "CreateProcessW ok, pid=" << pi.dwProcessId << std::endl;
        // Close handles we don't need
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        server_running_ = true;
        #else
        log << "startServer: using nohup" << std::endl;
        // Linux: Use nohup
        std::string cmd = "nohup ./build/Release/themis_server > /dev/null 2>&1 &";
        int result = std::system(cmd.c_str());
        log << "nohup result=" << result << std::endl;
        if (result != 0) {
            log.close();
            throw std::runtime_error("Failed to start server process");
        }
        server_running_ = true;
        #endif
        log << "startServer: server_running_ set true" << std::endl;
        log.close();
        
        // Wait for server to be ready (max 10 seconds)
        bool server_ready = false;
        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            bool ok = checkServerHealth();
            std::ofstream log2("tests\\server_start.log", std::ios::app);
            log2 << "startServer: health check iter=" << i << " => " << (ok?"OK":"NOTOK") << std::endl;
            log2.close();
            if (ok) { server_ready = true; break; }
        }
        
        if (!server_ready) {
            stopServer();
            std::ofstream log3("tests\\server_start.log", std::ios::app);
            log3 << "startServer: timed out waiting for health" << std::endl;
            log3.close();
            throw std::runtime_error("Server did not become ready within timeout");
        }
    }
    
    void stopServer() {
        if (!server_running_) return;
        
        #ifdef _WIN32
        std::system("powershell -NoProfile -Command \"Get-Process themis_server -ErrorAction SilentlyContinue | Stop-Process -Force\"");
        #else
        std::system("pkill -9 themis_server");
        #endif
        
        server_running_ = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    bool checkServerHealth() {
        try {
            auto response = httpGet("/health");
            bool ok = (response.result() == http::status::ok);
            std::ofstream log("tests\\server_start.log", std::ios::app);
            log << "checkServerHealth: /health response status=" << static_cast<int>(response.result()) << " ok=" << ok << std::endl;
            log.close();
            return ok;
        } catch (std::exception const& ex) {
            std::ofstream log("tests\\server_start.log", std::ios::app);
            log << "checkServerHealth: exception: " << ex.what() << std::endl;
            log.close();
            return false;
        } catch (...) {
            std::ofstream log("tests\\server_start.log", std::ios::app);
            log << "checkServerHealth: unknown exception" << std::endl;
            log.close();
            return false;
        }
    }
    
    http::response<http::string_body> httpGet(const std::string& target) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        
        auto const results = resolver.resolve("localhost", std::to_string(server_port_));
        stream.connect(results);
        
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::user_agent, "vccdb_test");
        
        http::write(stream, req);
        
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        
        return res;
    }
    
private:
    std::atomic<bool> server_running_;
    uint16_t server_port_;
};

class StatsApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        server_ = std::make_unique<ServerFixture>();
        server_->startServer();
        
        // Give server a moment to initialize stats
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void TearDown() override {
        server_->stopServer();
        server_.reset();
    }
    
    std::unique_ptr<ServerFixture> server_;
};

TEST_F(StatsApiTest, StatsEndpointReturnsValidJson) {
    auto response = server_->httpGet("/stats");
    
    ASSERT_EQ(response.result(), http::status::ok);
    ASSERT_EQ(response[http::field::content_type], "application/json");
    
    // Parse JSON
    json stats;
    ASSERT_NO_THROW(stats = json::parse(response.body()));
    
    // Verify top-level structure
    ASSERT_TRUE(stats.contains("server"));
    ASSERT_TRUE(stats.contains("storage"));
    ASSERT_TRUE(stats["server"].is_object());
    ASSERT_TRUE(stats["storage"].is_object());
}

TEST_F(StatsApiTest, ServerStatsContainsRequiredFields) {
    auto response = server_->httpGet("/stats");
    json stats = json::parse(response.body());
    
    ASSERT_TRUE(stats.contains("server"));
    json server = stats["server"];
    
    // Check all required server fields
    ASSERT_TRUE(server.contains("uptime_seconds"));
    ASSERT_TRUE(server.contains("total_requests"));
    ASSERT_TRUE(server.contains("total_errors"));
    ASSERT_TRUE(server.contains("queries_per_second"));
    ASSERT_TRUE(server.contains("threads"));
    
    // Verify types
    ASSERT_TRUE(server["uptime_seconds"].is_number());
    ASSERT_TRUE(server["total_requests"].is_number_unsigned());
    ASSERT_TRUE(server["total_errors"].is_number_unsigned());
    ASSERT_TRUE(server["queries_per_second"].is_number());
    ASSERT_TRUE(server["threads"].is_number());
    
    // Verify reasonable values
    ASSERT_GE(server["uptime_seconds"].get<int>(), 0);
    ASSERT_GE(server["total_requests"].get<uint64_t>(), 1); // At least the /stats request
    ASSERT_GE(server["total_errors"].get<uint64_t>(), 0);
    ASSERT_GE(server["queries_per_second"].get<double>(), 0.0);
    ASSERT_GT(server["threads"].get<int>(), 0);
}

TEST_F(StatsApiTest, StorageStatsContainsRocksDBMetrics) {
    auto response = server_->httpGet("/stats");
    json stats = json::parse(response.body());
    
    ASSERT_TRUE(stats.contains("storage"));
    json storage = stats["storage"];
    
    // Storage should contain rocksdb object
    ASSERT_TRUE(storage.contains("rocksdb"));
    json rocksdb = storage["rocksdb"];
    
    // Check for key RocksDB metrics
    ASSERT_TRUE(rocksdb.contains("block_cache_usage_bytes"));
    ASSERT_TRUE(rocksdb.contains("block_cache_capacity_bytes"));
    ASSERT_TRUE(rocksdb.contains("memtable_size_bytes"));
    
    // Verify types and reasonable values
    ASSERT_TRUE(rocksdb["block_cache_usage_bytes"].is_number_unsigned());
    ASSERT_TRUE(rocksdb["block_cache_capacity_bytes"].is_number_unsigned());
    ASSERT_GE(rocksdb["block_cache_capacity_bytes"].get<uint64_t>(), 0);
}

TEST_F(StatsApiTest, StorageStatsContainsFilesPerLevel) {
    auto response = server_->httpGet("/stats");
    json stats = json::parse(response.body());
    
    json rocksdb = stats["storage"]["rocksdb"];
    
    // Check for LSM-tree level information
    ASSERT_TRUE(rocksdb.contains("files_per_level"));
    ASSERT_TRUE(rocksdb["files_per_level"].is_object());
    
    // Should have at least L0
    json levels = rocksdb["files_per_level"];
    ASSERT_TRUE(levels.contains("L0"));
    
    // Each level should be a number
    for (auto& [key, value] : levels.items()) {
        ASSERT_TRUE(key.rfind("L", 0) == 0); // Starts with "L"
        ASSERT_TRUE(value.is_number());
    }
}

TEST_F(StatsApiTest, UptimeIncreases) {
    // Get initial stats
    auto response1 = server_->httpGet("/stats");
    json stats1 = json::parse(response1.body());
    int uptime1 = stats1["server"]["uptime_seconds"].get<int>();
    
    // Wait 2 seconds
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Get stats again
    auto response2 = server_->httpGet("/stats");
    json stats2 = json::parse(response2.body());
    int uptime2 = stats2["server"]["uptime_seconds"].get<int>();
    
    // Uptime should have increased by at least 1 second
    ASSERT_GE(uptime2, uptime1 + 1);
}

TEST_F(StatsApiTest, RequestCountIncreases) {
    // Get initial stats
    auto response1 = server_->httpGet("/stats");
    json stats1 = json::parse(response1.body());
    uint64_t requests1 = stats1["server"]["total_requests"].get<uint64_t>();
    
    // Make another request
    auto response2 = server_->httpGet("/health");
    ASSERT_EQ(response2.result(), http::status::ok);
    
    // Get stats again
    auto response3 = server_->httpGet("/stats");
    json stats3 = json::parse(response3.body());
    uint64_t requests3 = stats3["server"]["total_requests"].get<uint64_t>();
    
    // Request count should have increased by at least 2 (health + second stats)
    ASSERT_GE(requests3, requests1 + 2);
}

TEST_F(StatsApiTest, CacheUsageReasonable) {
    auto response = server_->httpGet("/stats");
    json stats = json::parse(response.body());
    
    json rocksdb = stats["storage"]["rocksdb"];
    
    uint64_t usage = rocksdb["block_cache_usage_bytes"].get<uint64_t>();
    uint64_t capacity = rocksdb["block_cache_capacity_bytes"].get<uint64_t>();
    
    // Usage should not exceed capacity
    ASSERT_LE(usage, capacity);
    
    // Capacity should be reasonable (configured to 1GB by default)
    ASSERT_GT(capacity, 0);
}
