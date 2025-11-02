#pragma once

#include <memory>
#include <string>
#include <nlohmann/json.hpp>
#include "utils/pii_detector.h"

namespace themis { namespace server {

/**
 * @brief Classification Admin API Handler
 * 
 * Provides REST endpoints for data classification:
 * - GET /api/classification/rules - List classification rules
 * - POST /api/classification/test - Test classification on sample data
 */
class ClassificationApiHandler {
public:
    /**
     * @brief Initialize with PII Detector for classification
     * @param pii_detector Shared pointer to PIIDetector instance
     */
    explicit ClassificationApiHandler(std::shared_ptr<themis::utils::PIIDetector> pii_detector);
    
    ClassificationApiHandler() = default;

    /**
     * @brief List all classification rules
     * @return JSON response: { "items": [...], "total": N }
     */
    nlohmann::json listRules();

    /**
     * @brief Test classification on sample data
     * @param body JSON with { "text": "...", "metadata": {...} }
     * @return JSON response: { "classification": "...", "confidence": 0.95, "detected_entities": [...] }
     */
    nlohmann::json testClassification(const nlohmann::json& body);
    
private:
    std::shared_ptr<themis::utils::PIIDetector> pii_detector_;
};

}} // namespace themis::server
