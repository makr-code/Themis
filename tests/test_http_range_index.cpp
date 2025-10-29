#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>

using json = nlohmann::json;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class ServerFixture2 {
public:
    ServerFixture2() : server_running_(false), server_port_(8765) {}
    ~ServerFixture2() { stopServer(); }

    void startServer() {
        if (server_running_) return;
        // If test runner already started a server, reuse it
        try {
            if (check("/health") == http::status::ok) {
                server_running_ = true;
                return;
            }
        } catch (...) {}

    #ifdef _WIN32
    // Start server via CreateProcess from the current test binary directory
    #include <windows.h>
    #include <filesystem>
    STARTUPINFOW si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    wchar_t modulePath[MAX_PATH];
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    std::filesystem::path exeDir = std::filesystem::path(modulePath).parent_path();
    std::filesystem::path appPath = exeDir / L"themis_server.exe";
    std::wstring app = appPath.wstring();
    std::filesystem::path rootDir = exeDir.parent_path().parent_path();
    std::wstring workDir = rootDir.wstring();
    BOOL ok = CreateProcessW(app.c_str(), nullptr, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi);
    if (!ok) {
        throw std::runtime_error("Failed to start server process (CreateProcess failed)");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    server_running_ = true;
    #else
    std::string cmd = "nohup ./build/Release/themis_server > /dev/null 2>&1 &";
    int result = std::system(cmd.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to start server process");
    }
    server_running_ = true;
    #endif
        bool ready=false;
        for (int i=0;i<50;++i){
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (check("/health") == http::status::ok) { ready=true; break; }
        }
        if (!ready) throw std::runtime_error("Server not ready");
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

    http::status check(const std::string& path) { return request(http::verb::get, path).result(); }

    http::response<http::string_body> postJson(const std::string& target, const json& j) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("localhost", std::to_string(server_port_));
        stream.connect(results);

        http::request<http::string_body> req{http::verb::post, target, 11};
        req.set(http::field::host, "localhost");
        req.set(http::field::content_type, "application/json");
        req.body() = j.dump();
        req.prepare_payload();

        http::write(stream, req);
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);
        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);
        return res;
    }

    http::response<http::string_body> request(http::verb method, const std::string& target) {
        net::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve("localhost", std::to_string(server_port_));
        stream.connect(results);

        http::request<http::string_body> req{method, target, 11};
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

class HttpRangeIndexTest : public ::testing::Test {
protected:
    void SetUp() override { server_ = std::make_unique<ServerFixture2>(); server_->startServer(); }
    void TearDown() override { server_->stopServer(); server_.reset(); }
    std::unique_ptr<ServerFixture2> server_;
};

TEST_F(HttpRangeIndexTest, CreateRangeIndex_Succeeds) {
    json body = {
        {"table", "users"},
        {"column", "age"},
        {"type", "range"}
    };
    auto res = server_->postJson("/index/create", body);
    ASSERT_EQ(res.result(), http::status::ok) << res.body();
    json out = json::parse(res.body());
    ASSERT_TRUE(out.contains("success") && out["success"].get<bool>());
    ASSERT_EQ(out["type"], "range");
}

TEST_F(HttpRangeIndexTest, CreateRangeIndex_Duplicate_IsIdempotent) {
    json body = {{"table","users"},{"column","salary"},{"type","range"}};
    auto r1 = server_->postJson("/index/create", body);
    ASSERT_EQ(r1.result(), http::status::ok) << r1.body();
    auto r2 = server_->postJson("/index/create", body);
    ASSERT_EQ(r2.result(), http::status::ok) << r2.body();
}

TEST_F(HttpRangeIndexTest, CreateRangeIndex_MissingColumn_400) {
    json body = {{"table","users"},{"type","range"}};
    auto r = server_->postJson("/index/create", body);
    ASSERT_EQ(r.result(), http::status::bad_request);
}

TEST_F(HttpRangeIndexTest, DropRangeIndex_Succeeds) {
    json create = {{"table","users"},{"column","price"},{"type","range"}};
    auto r1 = server_->postJson("/index/create", create);
    ASSERT_EQ(r1.result(), http::status::ok);

    json drop = {{"table","users"},{"column","price"},{"type","range"}};
    auto r2 = server_->postJson("/index/drop", drop);
    ASSERT_EQ(r2.result(), http::status::ok) << r2.body();
}
