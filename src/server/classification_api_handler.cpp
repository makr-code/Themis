#include "server/classification_api_handler.h"
#include "utils/logger.h"
#include "utils/pii_detection_engine.h"

namespace themis { namespace server {

ClassificationApiHandler::ClassificationApiHandler(std::shared_ptr<themis::utils::PIIDetector> pii_detector)
    : pii_detector_(pii_detector) {
}

nlohmann::json ClassificationApiHandler::listRules() {
    try {
        if (!pii_detector_) {
            THEMIS_WARN("Classification API: PIIDetector not initialized");
            return {
                {"items", nlohmann::json::array()},
                {"total", 0}
            };
        }
        
        // Return supported PII types as classification rules
        nlohmann::json items = nlohmann::json::array();
        
        std::vector<themis::utils::PIIType> supported_types = {
            themis::utils::PIIType::EMAIL,
            themis::utils::PIIType::PHONE,
            themis::utils::PIIType::SSN,
            themis::utils::PIIType::CREDIT_CARD,
            themis::utils::PIIType::IBAN,
            themis::utils::PIIType::IP_ADDRESS,
            themis::utils::PIIType::URL,
            themis::utils::PIIType::PERSON_NAME,
            themis::utils::PIIType::LOCATION,
            themis::utils::PIIType::ORGANIZATION
        };
        
        for (const auto& pii_type : supported_types) {
            const auto type_name = themis::utils::PIITypeUtils::toString(pii_type);
            items.push_back({
                {"type", type_name},
                {"description", type_name + " detection"},
                {"classification_level", "CONFIDENTIAL"},
                {"enabled", true}
            });
        }
        
        THEMIS_INFO("Classification API: Listed {} classification rules", items.size());
        
        return {
            {"items", items},
            {"total", static_cast<int>(items.size())}
        };
        
    } catch (const std::exception& ex) {
        THEMIS_ERROR("Classification API listRules failed: {}", ex.what());
        return {
            {"error", "Internal Server Error"},
            {"message", ex.what()},
            {"status_code", 500}
        };
    }
}

nlohmann::json ClassificationApiHandler::testClassification(const nlohmann::json& body) {
    try {
        if (!pii_detector_) {
            THEMIS_ERROR("Classification API: PIIDetector not initialized");
            return {
                {"error", "Service Unavailable"},
                {"message", "Classification service not available"},
                {"status_code", 503}
            };
        }
        
        // Extract text from request body
        if (!body.contains("text") || !body["text"].is_string()) {
            return {
                {"error", "Bad Request"},
                {"message", "Missing or invalid 'text' field in request body"},
                {"status_code", 400}
            };
        }
        
        std::string text = body["text"].get<std::string>();
        
        // Run PII detection
        auto findings = pii_detector_->detectInText(text);
        
        // Determine overall classification based on findings
        std::string classification = "PUBLIC";
        if (!findings.empty()) {
            // Any PII detection triggers CONFIDENTIAL classification
            classification = "CONFIDENTIAL";
        }
        
        // Build detected entities list
        nlohmann::json detected_entities = nlohmann::json::array();
        for (const auto& finding : findings) {
            detected_entities.push_back({
                {"type", themis::utils::PIITypeUtils::toString(finding.type)},
                {"start", finding.start_offset},
                {"end", finding.end_offset},
                {"confidence", finding.confidence},
                {"pattern", finding.pattern_name},
                {"engine", finding.engine_name}
            });
        }
        
        THEMIS_INFO("Classification API: Tested classification on {} chars, found {} entities -> {}",
                    text.length(), findings.size(), classification);
        
        return {
            {"classification", classification},
            {"confidence", findings.empty() ? 1.0 : 0.95},
            {"detected_entities", detected_entities},
            {"entity_count", static_cast<int>(findings.size())}
        };
        
    } catch (const std::exception& ex) {
        THEMIS_ERROR("Classification API testClassification failed: {}", ex.what());
        return {
            {"error", "Internal Server Error"},
            {"message", ex.what()},
            {"status_code", 500}
        };
    }
}

}} // namespace themis::server
