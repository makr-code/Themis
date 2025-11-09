#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace themis {

/**
 * @brief X.509 Certificate representation
 */
struct X509Certificate {
    std::string id;                    // Certificate ID (serial number)
    std::string pem;                   // PEM-encoded certificate
    std::string subject;               // Subject DN (e.g., "CN=themis-db")
    std::string issuer;                // Issuer DN (e.g., "CN=VCC-PKI-CA")
    int64_t not_before_ms;             // Valid from timestamp
    int64_t not_after_ms;              // Valid until timestamp
    std::string key_usage;             // Key usage (e.g., "encryption", "signing")
    std::vector<std::string> san;      // Subject Alternative Names
    
    bool isValid() const;
    bool isExpired(int64_t now_ms) const;
    nlohmann::json toJson() const;
    static X509Certificate fromJson(const nlohmann::json& j);
};

/**
 * @brief Certificate Revocation List entry
 */
struct CRLEntry {
    std::string serial_number;         // Revoked certificate serial
    int64_t revocation_time_ms;        // When was it revoked
    std::string reason;                // Revocation reason (e.g., "key-compromise")
    
    nlohmann::json toJson() const;
    static CRLEntry fromJson(const nlohmann::json& j);
};

/**
 * @brief Certificate request parameters
 */
struct CertificateRequest {
    std::string common_name;           // CN (e.g., "themis-db-node-1")
    std::string organization;          // O (e.g., "VCC GmbH")
    std::vector<std::string> san;      // Subject Alternative Names
    std::string key_usage;             // "encryption" or "signing"
    int64_t validity_days;             // Certificate validity period
    
    CertificateRequest()
        : organization("VCC GmbH")
        , key_usage("encryption")
        , validity_days(365)
    {}
    
    nlohmann::json toJson() const;
};

/**
 * @brief TLS configuration for VCCPKIClient
 */
struct TLSConfig {
    std::string ca_cert_path;          // Path to Root CA certificate
    std::string client_cert_path;      // Path to client certificate (mTLS)
    std::string client_key_path;       // Path to client private key (mTLS)
    bool verify_server;                // Verify server certificate
    bool use_mtls;                     // Enable mutual TLS
    
    TLSConfig()
        : verify_server(true)
        , use_mtls(false)
    {}
};

/**
 * @brief Client for VCC-PKI Server communication
 * 
 * Responsibilities:
 * - Request certificates from VCC-PKI Server
 * - Retrieve Certificate Revocation List (CRL)
 * - Validate certificates locally
 * - Handle TLS/mTLS connections
 * 
 * Architecture:
 * ```
 * ThemisDB → VCCPKIClient → HTTPS → VCC-PKI Server (Python)
 *                             ↓
 *                    TLS/mTLS Verification
 * ```
 * 
 * API Endpoints (VCC-PKI Server):
 * - GET  /api/v1/certificates/{id}          - Retrieve certificate
 * - POST /api/v1/certificates/request       - Request new certificate
 * - GET  /api/v1/crl                        - Get revocation list
 * - GET  /api/v1/health                     - Health check
 * 
 * Thread Safety:
 * - All methods are thread-safe
 * - Internal HTTP client uses connection pooling
 * 
 * Performance:
 * - Caching NOT implemented here (done by PKIKeyProvider)
 * - Timeout: 5s default, configurable
 * - Retry: 3 attempts with exponential backoff
 * 
 * Example Usage:
 * @code
 * TLSConfig tls;
 * tls.ca_cert_path = "/etc/themis/ca-root.pem";
 * tls.use_mtls = true;
 * tls.client_cert_path = "/etc/themis/client-cert.pem";
 * tls.client_key_path = "/etc/themis/client-key.pem";
 * 
 * VCCPKIClient client("https://pki-server:8443", tls);
 * 
 * // Request KEK certificate
 * CertificateRequest req;
 * req.common_name = "themis-kek-2025";
 * req.key_usage = "encryption";
 * auto cert = client.requestCertificate(req);
 * 
 * // Check CRL
 * auto crl = client.getCRL();
 * if (client.isRevoked(cert.id, crl)) {
 *     throw std::runtime_error("Certificate revoked!");
 * }
 * @endcode
 */
class VCCPKIClient {
public:
    /**
     * @brief Constructor
     * 
     * @param base_url VCC-PKI Server base URL (e.g., "https://localhost:8443")
     * @param tls_config TLS/mTLS configuration
     * @param timeout_ms HTTP request timeout in milliseconds (default: 5000)
     */
    explicit VCCPKIClient(
        const std::string& base_url,
        const TLSConfig& tls_config,
        int timeout_ms = 5000
    );
    
    ~VCCPKIClient();
    
    // Disable copy (HTTP client not copyable)
    VCCPKIClient(const VCCPKIClient&) = delete;
    VCCPKIClient& operator=(const VCCPKIClient&) = delete;
    
    // Enable move
    VCCPKIClient(VCCPKIClient&&) noexcept;
    VCCPKIClient& operator=(VCCPKIClient&&) noexcept;
    
    /**
     * @brief Request a new certificate from PKI server
     * 
     * @param request Certificate request parameters
     * @return Issued X.509 certificate
     * @throws std::runtime_error if request fails or server returns error
     */
    X509Certificate requestCertificate(const CertificateRequest& request);
    
    /**
     * @brief Retrieve an existing certificate by ID
     * 
     * @param cert_id Certificate ID (serial number or alias)
     * @return X.509 certificate
     * @throws std::runtime_error if certificate not found
     */
    X509Certificate getCertificate(const std::string& cert_id);
    
    /**
     * @brief Retrieve Certificate Revocation List
     * 
     * @return List of revoked certificates
     * @throws std::runtime_error if CRL retrieval fails
     */
    std::vector<CRLEntry> getCRL();
    
    /**
     * @brief Check if a certificate is revoked
     * 
     * @param cert_id Certificate ID to check
     * @param crl Certificate Revocation List
     * @return true if certificate is revoked, false otherwise
     */
    bool isRevoked(const std::string& cert_id, const std::vector<CRLEntry>& crl) const;
    
    /**
     * @brief Health check - verify PKI server is reachable
     * 
     * @return true if server is healthy, false otherwise
     */
    bool healthCheck();
    
    /**
     * @brief Get base URL of PKI server
     */
    const std::string& getBaseUrl() const { return base_url_; }
    
    /**
     * @brief Set HTTP timeout
     * 
     * @param timeout_ms Timeout in milliseconds
     */
    void setTimeout(int timeout_ms) { timeout_ms_ = timeout_ms; }
    
private:
    std::string base_url_;             // PKI server base URL
    TLSConfig tls_config_;             // TLS/mTLS configuration
    int timeout_ms_;                   // HTTP request timeout
    
    // HTTP client internals (using libcurl or Boost.Beast)
    struct Impl;
    std::unique_ptr<Impl> impl_;
    
    // Helper: Execute HTTP GET request
    std::string httpGet(const std::string& path);
    
    // Helper: Execute HTTP POST request
    std::string httpPost(const std::string& path, const nlohmann::json& body);
    
    // Helper: Parse X.509 certificate (PEM format)
    static X509Certificate parseCertificate(const std::string& pem);
    
    // Helper: Validate certificate chain
    bool validateCertChain(const X509Certificate& cert) const;
};

} // namespace themis
