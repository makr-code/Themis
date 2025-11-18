#include "server/pki_api_handler.h"
#include "utils/logger.h"
#include <openssl/sha.h>

namespace themis { namespace server {

static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val=0, valb=-6;
    for (uint8_t c : data) {
        val = (val<<8) + c;
        valb += 8;
        while (valb>=0) {
            out.push_back(chars[(val>>valb)&0x3F]);
            valb -= 6;
        }
    }
    if (valb>-6) out.push_back(chars[((val<<8)>>(valb+8))&0x3F]);
    while (out.size()%4) out.push_back('=');
    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& encoded) {
    std::vector<int> T(256, -1);
    const std::string b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i=0;i<64;i++) T[(unsigned char)b64_chars[i]] = i;
    std::vector<uint8_t> out;
    int val=0, valb=-8;
    for (unsigned char c : encoded) {
        if (T[c]==-1) break;
        val = (val<<6) + T[c];
        valb += 6;
        if (valb>=0) {
            out.push_back((uint8_t)((val>>valb)&0xFF));
            valb -= 8;
        }
    }
    return out;
}

PkiApiHandler::PkiApiHandler(std::shared_ptr<SigningService> signing_service)
    : signing_service_(std::move(signing_service)) {}

PkiApiHandler::PkiApiHandler(std::shared_ptr<SigningService> signing_service,
                             std::shared_ptr<security::HSMProvider> hsm_provider,
                             std::shared_ptr<security::TimestampAuthority> tsa)
    : signing_service_(std::move(signing_service)),
      hsm_provider_(std::move(hsm_provider)),
      tsa_(std::move(tsa)) {}

nlohmann::json PkiApiHandler::sign(const std::string& key_id, const nlohmann::json& body) {
    try {
        if (!signing_service_) {
            THEMIS_ERROR("PKI API: Signing service not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["status_code"] = 503; return j;
        }

        if (!body.contains("data_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing data_b64"; j["status_code"] = 400; return j;
        }

        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);

        SigningResult res = signing_service_->sign(data, key_id);

        nlohmann::json resp;
        resp["signature_b64"] = base64_encode(res.signature);
        resp["algorithm"] = res.algorithm;
        return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API sign failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

nlohmann::json PkiApiHandler::verify(const std::string& key_id, const nlohmann::json& body) {
    try {
        if (!signing_service_) {
            THEMIS_ERROR("PKI API: Signing service not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["status_code"] = 503; return j;
        }

        if (!body.contains("data_b64") || !body.contains("signature_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing data_b64 or signature_b64"; j["status_code"] = 400; return j;
        }

        auto data = base64_decode(body["data_b64"].get<std::string>());
        auto sig = base64_decode(body["signature_b64"].get<std::string>());

        bool ok = signing_service_->verify(data, sig, key_id);
        nlohmann::json resp; resp["valid"] = ok; return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API verify failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

// ============================================================================
// HSM Endpoints
// ============================================================================

nlohmann::json PkiApiHandler::hsmSign(const nlohmann::json& body) {
    try {
        if (!hsm_provider_) {
            THEMIS_ERROR("PKI API: HSM provider not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["message"] = "HSM not configured"; j["status_code"] = 503; return j;
        }

        if (!body.contains("data_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing data_b64"; j["status_code"] = 400; return j;
        }

        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);

        auto result = hsm_provider_->sign(data);

        if (!result.success) {
            nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = "HSM signing failed"; j["status_code"] = 500; return j;
        }

        nlohmann::json resp;
        resp["signature_b64"] = result.signature_b64;
        resp["algorithm"] = result.algorithm;
        resp["key_id"] = result.key_id;
        resp["cert_serial"] = result.cert_serial;
        return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API hsmSign failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

nlohmann::json PkiApiHandler::hsmListKeys() {
    try {
        if (!hsm_provider_) {
            THEMIS_ERROR("PKI API: HSM provider not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["message"] = "HSM not configured"; j["status_code"] = 503; return j;
        }

        auto keys = hsm_provider_->listKeys();

        {
            nlohmann::json resp;
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& k : keys) {
                nlohmann::json jk;
                jk["label"] = k.label;
                jk["id"] = k.id;
                jk["algorithm"] = k.algorithm;
                jk["can_sign"] = k.can_sign;
                jk["can_verify"] = k.can_verify;
                jk["extractable"] = k.extractable;
                jk["key_size"] = k.key_size;
                arr.push_back(std::move(jk));
            }
            resp["keys"] = std::move(arr);
            return resp;
        }

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API hsmListKeys failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

// ============================================================================
// Timestamp Authority Endpoints
// ============================================================================

nlohmann::json PkiApiHandler::getTimestamp(const nlohmann::json& body) {
    try {
        if (!tsa_) {
            THEMIS_ERROR("PKI API: TSA not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["message"] = "TSA not configured"; j["status_code"] = 503; return j;
        }

        if (!body.contains("data_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing data_b64"; j["status_code"] = 400; return j;
        }

        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);

        auto token = tsa_->getTimestamp(data);

        if (!token.success) {
            nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = "TSA request failed"; j["status_code"] = 500; return j;
        }

        nlohmann::json resp;
        resp["timestamp_token_b64"] = token.token_b64;
        resp["timestamp_utc"] = token.timestamp_utc;
        resp["serial_number"] = token.serial_number;
        return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API getTimestamp failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

nlohmann::json PkiApiHandler::verifyTimestamp(const nlohmann::json& body) {
    try {
        if (!tsa_) {
            THEMIS_ERROR("PKI API: TSA not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["message"] = "TSA not configured"; j["status_code"] = 503; return j;
        }

        if (!body.contains("timestamp_token_b64") || !body.contains("data_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing timestamp_token_b64 or data_b64"; j["status_code"] = 400; return j;
        }

        std::string token_b64 = body["timestamp_token_b64"].get<std::string>();
        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);
        auto token = tsa_->parseToken(token_b64);
        bool ok = tsa_->verifyTimestamp(data, token);
        nlohmann::json resp;
        resp["valid"] = ok;
        resp["timestamp_utc"] = token.timestamp_utc;
        resp["serial_number"] = token.serial_number;
        return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API verifyTimestamp failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

// ============================================================================
// eIDAS Qualified Signature Endpoints
// ============================================================================

nlohmann::json PkiApiHandler::eidasSign(const nlohmann::json& body) {
    try {
        if (!hsm_provider_ || !tsa_) {
            THEMIS_ERROR("PKI API: HSM or TSA not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["message"] = "eIDAS signing requires HSM and TSA"; j["status_code"] = 503; return j;
        }

        if (!body.contains("data_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing data_b64"; j["status_code"] = 400; return j;
        }

        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);

        // Step 1: Sign with HSM
        auto hsm_result = hsm_provider_->sign(data);
        if (!hsm_result.success) {
            nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = "HSM signing failed"; j["status_code"] = 500; return j;
        }

        // Step 2: Get timestamp for signature
        auto signature_bytes = base64_decode(hsm_result.signature_b64);
        auto ts_token = tsa_->getTimestamp(signature_bytes);
        if (!ts_token.success) {
            THEMIS_WARN("PKI API: Timestamp failed, signature created without timestamp");
        }

        // Step 3: Create qualified signature object (CAdES-like structure)
        nlohmann::json qualified_sig;
        qualified_sig["signature_b64"] = hsm_result.signature_b64;
        qualified_sig["algorithm"] = hsm_result.algorithm;
        qualified_sig["key_id"] = hsm_result.key_id;
        qualified_sig["cert_serial"] = hsm_result.cert_serial;
        qualified_sig["timestamp_token_b64"] = ts_token.success ? ts_token.token_b64 : std::string("");
        qualified_sig["timestamp_utc"] = ts_token.success ? ts_token.timestamp_utc : std::string("");
        qualified_sig["format"] = "eIDAS-QES";
        qualified_sig["version"] = "1.0";

        nlohmann::json resp;
        resp["qualified_signature"] = qualified_sig;
        resp["timestamped"] = ts_token.success;
        return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API eidasSign failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

nlohmann::json PkiApiHandler::eidasVerify(const nlohmann::json& body) {
    try {
        if (!hsm_provider_ || !tsa_) {
            THEMIS_ERROR("PKI API: HSM or TSA not initialized");
            nlohmann::json j; j["error"] = "Service Unavailable"; j["message"] = "eIDAS verification requires HSM and TSA"; j["status_code"] = 503; return j;
        }

        if (!body.contains("qualified_signature") || !body.contains("data_b64")) {
            nlohmann::json j; j["error"] = "Bad Request"; j["message"] = "missing qualified_signature or data_b64"; j["status_code"] = 400; return j;
        }

        auto qualified_sig = body["qualified_signature"];
        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);

        // Step 1: Verify signature
        if (!qualified_sig.contains("signature_b64")) {
            return {{"error","Bad Request"},{"message","missing signature_b64 in qualified_signature"},{"status_code",400}};
        }

        std::string sig_b64 = qualified_sig["signature_b64"].get<std::string>();
        auto signature_bytes = base64_decode(sig_b64);
        bool sig_valid = hsm_provider_->verify(data, sig_b64);

        // Step 2: Verify timestamp if present
        bool ts_valid = true;
        if (qualified_sig.contains("timestamp_token_b64") && 
            !qualified_sig["timestamp_token_b64"].get<std::string>().empty()) {
            
            std::string ts_token_b64 = qualified_sig["timestamp_token_b64"].get<std::string>();
            auto ts_token = tsa_->parseToken(ts_token_b64);
            ts_valid = tsa_->verifyTimestamp(signature_bytes, ts_token);
        }

        nlohmann::json resp;
        resp["valid"] = sig_valid && ts_valid;
        resp["signature_valid"] = sig_valid;
        resp["timestamp_valid"] = ts_valid;
        resp["format"] = qualified_sig.value("format", "unknown");
        resp["algorithm"] = qualified_sig.value("algorithm", "unknown");
        resp["timestamp_utc"] = qualified_sig.value("timestamp_utc", "");
        return resp;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API eidasVerify failed: {}", ex.what());
        nlohmann::json j; j["error"] = "Internal Server Error"; j["message"] = ex.what(); j["status_code"] = 500; return j;
    }
}

// ============================================================================
// Certificate Management Endpoints
// ============================================================================

nlohmann::json PkiApiHandler::listCertificates() {
    try {
        // Placeholder: In production, integrate with certificate store
        // Could use OpenSSL's X509_STORE or HSM certificate enumeration
        
        THEMIS_INFO("PKI API: Listing certificates (stub implementation)");
        
        return {
            {"certificates", nlohmann::json::array()},
            {"message", "Certificate enumeration not yet implemented"}
        };

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API listCertificates failed: {}", ex.what());
        return {{"error","Internal Server Error"},{"message",ex.what()},{"status_code",500}};
    }
}

nlohmann::json PkiApiHandler::getCertificate(const std::string& cert_id) {
    try {
        // Placeholder: In production, retrieve from certificate store
        
        THEMIS_INFO("PKI API: Getting certificate {} (stub implementation)", cert_id);
        
        return {
            {"error", "Not Found"},
            {"message", "Certificate retrieval not yet implemented"},
            {"status_code", 404}
        };

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API getCertificate failed: {}", ex.what());
        return {{"error","Internal Server Error"},{"message",ex.what()},{"status_code",500}};
    }
}

// ============================================================================
// Status & Health Check Endpoint
// ============================================================================

nlohmann::json PkiApiHandler::getStatus() {
    try {
        nlohmann::json status = {
            {"signing_service", signing_service_ ? "available" : "unavailable"},
            {"hsm", hsm_provider_ ? "available" : "unavailable"},
            {"tsa", tsa_ ? "available" : "unavailable"}
        };

        // Check HSM connectivity
        if (hsm_provider_) {
            try {
                auto keys = hsm_provider_->listKeys();
                status["hsm_keys_count"] = keys.size();
                status["hsm_status"] = "connected";
            } catch (...) {
                status["hsm_status"] = "error";
            }
        }

        // Check TSA connectivity
        if (tsa_) {
            status["tsa_status"] = "configured";
            // Could perform a test timestamp request here
        }

        bool all_ok = signing_service_ && hsm_provider_ && tsa_;
        status["overall"] = all_ok ? "healthy" : "degraded";

        return status;

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API getStatus failed: {}", ex.what());
        return {{"error","Internal Server Error"},{"message",ex.what()},{"status_code",500}};
    }
}

}} // namespace themis::server
