#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace themis {
namespace utils {

/**
 * Cursor encoding/decoding utilities for pagination.
 * 
 * Cursors encode the last seen primary key or index position to enable
 * stateless pagination. Format: base64(json({pk: "...", collection: "..."}))
 */
class Cursor {
public:
    /**
     * Encode a cursor from the last primary key and collection name.
     * 
     * @param last_pk The primary key of the last item in the current page
     * @param collection The collection name
     * @return Base64-encoded cursor token
     */
    static std::string encode(const std::string& last_pk, const std::string& collection);
    
    /**
     * Decode a cursor token to extract the primary key and collection.
     * 
     * @param cursor_token Base64-encoded cursor string
     * @return Optional pair of (pk, collection), nullopt if invalid/expired
     */
    static std::optional<std::pair<std::string, std::string>> decode(const std::string& cursor_token);
    
private:
    // Base64 encode/decode helpers
    static std::string base64Encode(const std::string& input);
    static std::optional<std::string> base64Decode(const std::string& input);
};

/**
 * Paginated response structure for AQL queries.
 */
struct PaginatedResponse {
    nlohmann::json items;           // Array of result items
    bool has_more = false;          // True if there are more results
    std::string next_cursor;        // Cursor for next page (empty if no more)
    size_t batch_size = 0;          // Number of items in current batch
    
    nlohmann::json toJSON() const {
        nlohmann::json result = {
            {"items", items},
            {"has_more", has_more},
            {"batch_size", batch_size}
        };
        
        if (has_more && !next_cursor.empty()) {
            result["next_cursor"] = next_cursor;
        }
        
        return result;
    }
};

} // namespace utils
} // namespace themis
