#include "server/saga_api_handler.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace themis {
namespace server {

// Helper: Base64 encode
static std::string base64_encode_local(const std::vector<uint8_t>& data) {
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | (data[i + 2]);
        out.push_back(b64[(n >> 18) & 63]);
        out.push_back(b64[(n >> 12) & 63]);
        out.push_back(b64[(n >> 6) & 63]);
        out.push_back(b64[n & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t n = (data[i] << 16);
        out.push_back(b64[(n >> 18) & 63]);
        out.push_back(b64[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == data.size()) {
        uint32_t n = (data[i] << 16) | (data[i + 1] << 8);
        out.push_back(b64[(n >> 18) & 63]);
        out.push_back(b64[(n >> 12) & 63]);
        out.push_back(b64[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

SAGAApiHandler::SAGAApiHandler(std::shared_ptr<themis::utils::SAGALogger> saga_logger)
    : saga_logger_(std::move(saga_logger)) {}

nlohmann::json SAGABatchInfo::toJson() const {
    nlohmann::json j;
    j["batch_id"] = batch_id;
    j["start_time_ms"] = start_time_ms;
    j["end_time_ms"] = end_time_ms;
    j["entry_count"] = entry_count;
    j["lek_id"] = lek_id;
    j["key_version"] = key_version;
    j["signature_valid"] = signature_valid;
    j["signature_id"] = signature_id;
    j["cert_serial"] = cert_serial;
    j["algorithm"] = algorithm;
    
    // ISO 8601 timestamps
    auto format_ts = [](int64_t ms) -> std::string {
        auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time_t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return std::string(buf) + "Z";
    };
    
    j["start_time"] = format_ts(start_time_ms);
    j["end_time"] = format_ts(end_time_ms);
    j["duration_seconds"] = (end_time_ms - start_time_ms) / 1000;
    
    return j;
}

nlohmann::json SAGABatchDetail::toJson() const {
    nlohmann::json j = info.toJson();
    
    j["steps"] = nlohmann::json::array();
    for (const auto& step : steps) {
        auto step_ts = std::chrono::duration_cast<std::chrono::milliseconds>(
            step.timestamp.time_since_epoch()).count();
        
        auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(step_ts));
        auto time_t = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::gmtime(&time_t);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        std::string ts_str = std::string(buf) + "Z";
        
        nlohmann::json step_json = {
            {"saga_id", step.saga_id},
            {"step_name", step.step_name},
            {"action", step.action},
            {"entity_id", step.entity_id},
            {"status", step.status},
            {"timestamp", ts_str},
            {"payload", step.payload}
        };
        j["steps"].push_back(step_json);
    }
    
    j["ciphertext_hash_b64"] = ciphertext_hash_b64;
    j["signature_b64"] = signature_b64;
    
    return j;
}

SAGABatchInfo SAGAApiHandler::parseBatchInfo(const std::string& batch_id) {
    SAGABatchInfo info;
    info.batch_id = batch_id;
    
    // Read signature file to get batch metadata
    std::ifstream ifs("data/logs/saga_signatures.jsonl");
    if (!ifs.is_open()) {
        return info; // Return empty info if file doesn't exist
    }
    
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        
        try {
            auto j = nlohmann::json::parse(line);
            if (j.value("batch_id", "") == batch_id) {
                info.entry_count = j.value("entry_count", 0);
                info.lek_id = j.value("lek_id", "");
                info.key_version = j.value("key_version", 0);
                
                // Parse timestamps
                auto start_tp = std::chrono::system_clock::from_time_t(j.value("start_time", 0));
                auto end_tp = std::chrono::system_clock::from_time_t(j.value("end_time", 0));
                info.start_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    start_tp.time_since_epoch()).count();
                info.end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_tp.time_since_epoch()).count();
                
                // Parse signature info
                if (j.contains("signature") && j["signature"].is_object()) {
                    auto sig = j["signature"];
                    info.signature_valid = sig.value("ok", false);
                    info.signature_id = sig.value("id", "");
                    info.cert_serial = sig.value("cert_serial", "");
                    info.algorithm = sig.value("algorithm", "");
                }
                
                break;
            }
        } catch (...) {
            continue;
        }
    }
    
    return info;
}

nlohmann::json SAGAApiHandler::listBatches() {
    if (!saga_logger_) {
        return {{"error", "SAGA logger not initialized"}};
    }
    
    try {
        auto batch_ids = saga_logger_->listBatches();
        
        nlohmann::json result;
        result["batches"] = nlohmann::json::array();
        result["total_count"] = batch_ids.size();
        
        for (const auto& batch_id : batch_ids) {
            auto info = parseBatchInfo(batch_id);
            if (!info.batch_id.empty()) {
                result["batches"].push_back(info.toJson());
            }
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return {{"error", std::string("Failed to list batches: ") + e.what()}};
    }
}

nlohmann::json SAGAApiHandler::getBatchDetail(const std::string& batch_id) {
    if (!saga_logger_) {
        return {{"error", "SAGA logger not initialized"}};
    }
    
    try {
        auto info = parseBatchInfo(batch_id);
        if (info.batch_id.empty()) {
            return {{"error", "Batch not found"}};
        }
        
        // Verify and load batch
        bool verified = saga_logger_->verifyBatch(batch_id);
        info.signature_valid = verified;
        
        SAGABatchDetail detail;
        detail.info = info;
        
        if (verified) {
            detail.steps = saga_logger_->loadBatch(batch_id);
        }
        
        // Read signature data for hash and signature
        std::ifstream ifs("data/logs/saga_signatures.jsonl");
        if (ifs.is_open()) {
            std::string line;
            while (std::getline(ifs, line)) {
                if (line.empty()) continue;
                try {
                    auto j = nlohmann::json::parse(line);
                    if (j.value("batch_id", "") == batch_id) {
                        if (j.contains("ciphertext_hash") && j["ciphertext_hash"].is_array()) {
                            std::vector<uint8_t> hash;
                            for (auto& byte : j["ciphertext_hash"]) {
                                hash.push_back(byte.get<uint8_t>());
                            }
                            detail.ciphertext_hash_b64 = base64_encode_local(hash);
                        }
                        if (j.contains("signature") && j["signature"].contains("sig_b64")) {
                            detail.signature_b64 = j["signature"]["sig_b64"].get<std::string>();
                        }
                        break;
                    }
                } catch (...) {
                    continue;
                }
            }
        }
        
        return detail.toJson();
        
    } catch (const std::exception& e) {
        return {{"error", std::string("Failed to get batch detail: ") + e.what()}};
    }
}

nlohmann::json SAGAApiHandler::verifyBatch(const std::string& batch_id) {
    if (!saga_logger_) {
        return {{"error", "SAGA logger not initialized"}};
    }
    
    try {
        bool verified = saga_logger_->verifyBatch(batch_id);
        
        nlohmann::json result;
        result["batch_id"] = batch_id;
        result["verified"] = verified;
        result["message"] = verified ? "Signature valid, no tampering detected" 
                                      : "Verification failed - possible tampering";
        
        if (verified) {
            auto info = parseBatchInfo(batch_id);
            result["entry_count"] = info.entry_count;
            result["lek_id"] = info.lek_id;
            result["key_version"] = info.key_version;
        }
        
        return result;
        
    } catch (const std::exception& e) {
        return {
            {"batch_id", batch_id},
            {"verified", false},
            {"error", std::string("Verification error: ") + e.what()}
        };
    }
}

nlohmann::json SAGAApiHandler::flushCurrentBatch() {
    if (!saga_logger_) {
        return {{"error", "SAGA logger not initialized"}};
    }
    
    try {
        saga_logger_->flush();
        return {
            {"status", "flushed"},
            {"message", "Current batch has been signed and flushed"}
        };
    } catch (const std::exception& e) {
        return {{"error", std::string("Flush failed: ") + e.what()}};
    }
}

} // namespace server
} // namespace themis
