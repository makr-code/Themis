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

nlohmann::json PkiApiHandler::sign(const std::string& key_id, const nlohmann::json& body) {
    try {
        if (!signing_service_) {
            THEMIS_ERROR("PKI API: Signing service not initialized");
            return {{"error","Service Unavailable"},{"status_code",503}};
        }

        if (!body.contains("data_b64")) {
            return {{"error","Bad Request"},{"message","missing data_b64"},{"status_code",400}};
        }

        std::string data_b64 = body["data_b64"].get<std::string>();
        auto data = base64_decode(data_b64);

        SigningResult res = signing_service_->sign(data, key_id);

        return {
            {"signature_b64", base64_encode(res.signature)},
            {"algorithm", res.algorithm}
        };

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API sign failed: {}", ex.what());
        return {{"error","Internal Server Error"},{"message",ex.what()},{"status_code",500}};
    }
}

nlohmann::json PkiApiHandler::verify(const std::string& key_id, const nlohmann::json& body) {
    try {
        if (!signing_service_) {
            THEMIS_ERROR("PKI API: Signing service not initialized");
            return {{"error","Service Unavailable"},{"status_code",503}};
        }

        if (!body.contains("data_b64") || !body.contains("signature_b64")) {
            return {{"error","Bad Request"},{"message","missing data_b64 or signature_b64"},{"status_code",400}};
        }

        auto data = base64_decode(body["data_b64"].get<std::string>());
        auto sig = base64_decode(body["signature_b64"].get<std::string>());

        bool ok = signing_service_->verify(data, sig, key_id);

        return {{"valid", ok}};

    } catch (const std::exception& ex) {
        THEMIS_ERROR("PKI API verify failed: {}", ex.what());
        return {{"error","Internal Server Error"},{"message",ex.what()},{"status_code",500}};
    }
}

}} // namespace themis::server
