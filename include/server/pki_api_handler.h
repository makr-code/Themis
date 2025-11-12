#pragma once

#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "security/signing.h"

namespace themis { namespace server {

class PkiApiHandler {
public:
    explicit PkiApiHandler(std::shared_ptr<SigningService> signing_service);

    // Sign data (expects JSON body with "data_b64")
    nlohmann::json sign(const std::string& key_id, const nlohmann::json& body);

    // Verify signature (expects JSON body with "data_b64" and "signature_b64")
    nlohmann::json verify(const std::string& key_id, const nlohmann::json& body);

private:
    std::shared_ptr<SigningService> signing_service_;
};

}} // namespace themis::server
