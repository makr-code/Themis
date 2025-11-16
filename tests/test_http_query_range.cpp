#include <gtest/gtest.h>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

class HttpQueryRangeFixture {
public:
    HttpQueryRangeFixture() : server_running_(false), server_port_(8765) {}
    ~HttpQueryRangeFixture() { stopServer(); }

    void startServer() {
        if (server_running_) return;
        try {
            if (check("/health") == http::status::ok) { server_running_ = true; return; }
        } catch (...) {}
#ifdef _WIN32
    STARTUPINFOW si{}; PROCESS_INFORMATION pi{}; si.cb = sizeof(si);
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    std::filesystem::path appPath = exeDir / L"themis_server.exe";
    std::wstring app = appPath.wstring();
    std::filesystem::path rootDir = exeDir.parent_path().parent_path();
    std::wstring workDir = rootDir.wstring();
    BOOL ok = CreateProcessW(app.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
        if (!ok) throw std::runtime_error("Failed to start server process");
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
#else
        std::system("nohup ./build/Release/themis_server > /dev/null 2>&1 &");
#endif
        server_running_ = true;
        bool ready=false;
        for (int i=0;i<50;++i){ std::this_thread::sleep_for(std::chrono::milliseconds(200)); if (check("/health") == http::status::ok) { ready=true; break; } }
        if (!ready) throw std::runtime_error("Server did not become ready");
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

    http::status check(const std::string& p) {
        try {
            return get(p).result();
        } catch (...) { return http::status::internal_server_error; }
    }
    
    http::response<http::string_body> get(const std::string& target) {
        net::io_context ioc; 
        tcp::resolver res(ioc); 
        beast::tcp_stream stream(ioc);
        stream.connect(res.resolve("localhost", std::to_string(server_port_)));
        http::request<http::string_body> req{http::verb::get, target, 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::connection, "close");
        http::write(stream, req);
        beast::flat_buffer buf; 
        http::response<http::string_body> r;
        http::read(stream, buf, r);
        beast::error_code ec; 
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return r;
    }
    
    http::response<http::string_body> post(const std::string& target, const json& body) {
        net::io_context ioc; 
        tcp::resolver res(ioc); 
        beast::tcp_stream stream(ioc);
        auto results = res.resolve("localhost", std::to_string(server_port_));
        stream.connect(results);
        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::content_type, "application/json");
        req.set(http::field::connection, "close");
        req.body() = body.dump();
        req.prepare_payload();
        http::write(stream, req);
        beast::flat_buffer buf; 
        http::response<http::string_body> r;
        http::read(stream, buf, r);
        beast::error_code ec; 
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return r;
    }
private:
    bool server_running_;
    uint16_t server_port_;
};

class HttpQueryRangeTest : public ::testing::Test {
protected:
    void SetUp() override { server_ = std::make_unique<HttpQueryRangeFixture>(); server_->startServer(); }
    void TearDown() override { server_->stopServer(); server_.reset(); }
    std::unique_ptr<HttpQueryRangeFixture> server_;
};

TEST_F(HttpQueryRangeTest, DISABLED_CreateRangeIndex_AndQueryWithRange) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Create range index
    auto r1 = server_->post("/index/create", {{"table","users"},{"column","age"},{"type","range"}});
    ASSERT_EQ(r1.result(), http::status::ok) << r1.body();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Insert entities
    for (int i : {20,25,30,35}) {
        std::string pk = "user" + std::to_string(i);
        json ent = {{"key", "users:" + pk}, {"blob", json{{"age", std::to_string(i)}}.dump()}};
        auto r = server_->post("/entities", ent);
        ASSERT_EQ(r.result(), http::status::created) << r.body();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Query with range predicate: age >= 25 AND age <= 35
    json q = {
        {"table", "users"},
        {"range", json::array({{{"column","age"},{"gte","25"},{"lte","35"}}})},
        {"return", "keys"},
        {"allow_full_scan", true}
    };
    auto r2 = server_->post("/query", q);
    ASSERT_EQ(r2.result(), http::status::ok) << r2.body();
    json resp = json::parse(r2.body());
    ASSERT_TRUE(resp.contains("keys"));
    auto keys = resp["keys"];
    ASSERT_GE(keys.size(), 3u);
    // Expect user25, user30, user35
}

TEST_F(HttpQueryRangeTest, DISABLED_QueryWithOrderBy) {
    // Create range index
    auto r1 = server_->post("/index/create", {{"table","products"},{"column","price"},{"type","range"}});
    ASSERT_EQ(r1.result(), http::status::ok);

    // Insert products
    for (auto p : {50,100,150,200}) {
        std::string pk = "prod" + std::to_string(p);
        json ent = {{"key", "products:" + pk}, {"blob", json{{"price", std::to_string(p)}}.dump()}};
        auto r = server_->post("/entities", ent);
        ASSERT_EQ(r.result(), http::status::created) << r.body();
    }

    // Query with ORDER BY desc, limit 2
    json q = {
        {"table", "products"},
        {"order_by", {{"column","price"},{"desc",true},{"limit",2}}},
        {"return", "keys"},
        {"allow_full_scan", true}
    };
    auto r2 = server_->post("/query", q);
    ASSERT_EQ(r2.result(), http::status::ok) << r2.body();
    json resp = json::parse(r2.body());
    ASSERT_TRUE(resp.contains("keys"));
    auto keys = resp["keys"];
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0].get<std::string>(), "prod200");
    EXPECT_EQ(keys[1].get<std::string>(), "prod150");
}

TEST_F(HttpQueryRangeTest, DISABLED_CombineRangeAndOrderBy) {
    // Create range index
    auto r1 = server_->post("/index/create", {{"table","events"},{"column","timestamp"},{"type","range"}});
    ASSERT_EQ(r1.result(), http::status::ok);

    // Insert events (timestamps as strings for lexicographic order)
    for (auto t : {"2025-10-27T10:00:00", "2025-10-27T11:00:00", "2025-10-27T12:00:00", "2025-10-27T13:00:00"}) {
        std::string tstr = t;
        std::string safe_t = tstr;
        std::replace(safe_t.begin(), safe_t.end(), ':', '_');
        std::string pk = std::string("evt_") + safe_t;
        // Keep the timestamp value in the blob as the original (with colons)
        json ent = {{"key", "events:" + pk}, {"blob", json{{"timestamp", t}}.dump()}};
        auto r = server_->post("/entities", ent);
        ASSERT_EQ(r.result(), http::status::created) << r.body();
    }

    // Query: timestamp >= 11:00 AND timestamp <= 13:00, ORDER BY timestamp ASC, limit 2
    json q = {
        {"table", "events"},
        {"range", json::array({{{"column","timestamp"},{"gte","2025-10-27T11:00:00"},{"lte","2025-10-27T13:00:00"}}})},
        {"order_by", {{"column","timestamp"},{"desc",false},{"limit",2}}},
        {"return", "keys"},
        {"allow_full_scan", true}
    };
    auto r2 = server_->post("/query", q);
    ASSERT_EQ(r2.result(), http::status::ok) << r2.body();
    json resp = json::parse(r2.body());
    ASSERT_TRUE(resp.contains("keys"));
    auto keys = resp["keys"];
    ASSERT_EQ(keys.size(), 2u);
    EXPECT_EQ(keys[0].get<std::string>(), "evt_2025-10-27T11_00_00");
    EXPECT_EQ(keys[1].get<std::string>(), "evt_2025-10-27T12_00_00");
}
