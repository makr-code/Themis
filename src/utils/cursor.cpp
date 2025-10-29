#include "utils/cursor.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace themis {
namespace utils {

// Simple Base64 encoding table
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string Cursor::base64Encode(const std::string& input) {
    std::string output;
    int val = 0;
    int valb = -6;
    
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            output.push_back(base64_chars[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    
    if (valb > -6) {
        output.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
    }
    
    while (output.size() % 4) {
        output.push_back('=');
    }
    
    return output;
}

std::optional<std::string> Cursor::base64Decode(const std::string& input) {
    if (input.empty()) {
        return std::nullopt;
    }
    
    std::string output;
    std::vector<int> T(256, -1);
    
    for (int i = 0; i < 64; i++) {
        T[base64_chars[i]] = i;
    }
    
    int val = 0;
    int valb = -8;
    
    for (unsigned char c : input) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            output.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return output;
}

std::string Cursor::encode(const std::string& last_pk, const std::string& collection) {
    nlohmann::json cursor_data = {
        {"pk", last_pk},
        {"collection", collection},
        {"version", 1}  // For future compatibility
    };
    
    std::string json_str = cursor_data.dump();
    return base64Encode(json_str);
}

std::optional<std::pair<std::string, std::string>> Cursor::decode(const std::string& cursor_token) {
    auto decoded = base64Decode(cursor_token);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    
    try {
        auto cursor_data = nlohmann::json::parse(*decoded);
        
        // Validate cursor structure
        if (!cursor_data.contains("pk") || !cursor_data.contains("collection")) {
            return std::nullopt;
        }
        
        std::string pk = cursor_data["pk"].get<std::string>();
        std::string collection = cursor_data["collection"].get<std::string>();
        
        return std::make_pair(pk, collection);
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
}

} // namespace utils
} // namespace themis
