#pragma once

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

namespace themis { namespace server {

/**
 * @brief Reports Admin API Handler (Skeleton)
 * 
 * Provides REST endpoints for compliance reports:
 * - GET /api/reports/compliance - Generate compliance overview
 */
class ReportsApiHandler {
public:
    ReportsApiHandler() = default;

    /**
     * @brief Generate compliance report
     * @param report_type Type of report (e.g., "dsgvo", "sox", "overview")
     * @return JSON response: { "report_type": "...", "generated_at": "...", "metrics": {...} }
     */
    nlohmann::json generateComplianceReport(const std::string& report_type);
};

}} // namespace themis::server
