#include "storage/security_signature.h"
#include <stdexcept>

namespace themis {
namespace storage {

using json = nlohmann::json;

nlohmann::json SecuritySignature::toJson() const {
    json j;
    j["resource_id"] = resource_id;
    j["hash"] = hash;
    j["algorithm"] = algorithm;
    j["created_at"] = created_at;
    if (!created_by.empty()) {
        j["created_by"] = created_by;
    }
    if (!comment.empty()) {
        j["comment"] = comment;
    }
    return j;
}

std::optional<SecuritySignature> SecuritySignature::fromJson(const nlohmann::json& j) {
    try {
        SecuritySignature sig;
        sig.resource_id = j.at("resource_id").get<std::string>();
        sig.hash = j.at("hash").get<std::string>();
        sig.algorithm = j.at("algorithm").get<std::string>();
        sig.created_at = j.at("created_at").get<uint64_t>();
        
        if (j.contains("created_by")) {
            sig.created_by = j["created_by"].get<std::string>();
        }
        if (j.contains("comment")) {
            sig.comment = j["comment"].get<std::string>();
        }
        
        return sig;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string SecuritySignature::serialize() const {
    return toJson().dump();
}

std::optional<SecuritySignature> SecuritySignature::deserialize(const std::string& data) {
    try {
        json j = json::parse(data);
        return fromJson(j);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace storage
} // namespace themis
