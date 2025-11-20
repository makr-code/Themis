#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <memory>
#include <algorithm>

namespace themis::sharding {

/**
 * Shard Certificate Information
 * 
 * Represents X.509 certificate extensions specific to shard identity.
 * These extensions are parsed from the certificate and used for:
 * - Shard identification and authentication
 * - Capability-based access control
 * - Token range assignment
 * - Datacenter/rack locality
 */
struct ShardCertificateInfo {
    // Standard certificate fields
    std::string subject_cn;          // Common Name (e.g., "shard-001.themis.local")
    std::string serial_number;       // Certificate serial number (hex)
    std::string issuer_cn;           // Issuer CN (e.g., "themis-cluster-ca")
    std::string not_before;          // Validity start (ISO 8601)
    std::string not_after;           // Validity end (ISO 8601)
    
    // Subject Alternative Names
    std::vector<std::string> san_dns;  // DNS names
    std::vector<std::string> san_ip;   // IP addresses
    std::vector<std::string> san_uri;  // URIs (e.g., "urn:themis:shard:cluster-prod:001")
    
    // Custom X.509 Extensions for Sharding
    std::string shard_id;              // Shard identifier (e.g., "shard_001")
    std::string datacenter;            // Datacenter location (e.g., "dc1")
    std::string rack;                  // Rack location (e.g., "rack01")
    uint64_t token_range_start;        // Hash range start
    uint64_t token_range_end;          // Hash range end
    std::vector<std::string> capabilities; // read, write, replicate, admin
    std::string role;                  // primary, replica
    
    /**
     * Check if certificate has a specific capability
     */
    bool hasCapability(const std::string& cap) const {
        return std::find(capabilities.begin(), capabilities.end(), cap) != capabilities.end();
    }
    
    /**
     * Check if certificate is currently valid (time-based)
     */
    bool isValidNow() const;
};

/**
 * PKI Shard Certificate Parser
 * 
 * Parses X.509 certificates with custom extensions for shard identification.
 * Integrates with existing VCCPKIClient for signature operations.
 */
class PKIShardCertificate {
public:
    /**
     * Parse certificate from PEM file
     * @param cert_path Path to PEM-encoded certificate
     * @return Certificate info if successful, nullopt otherwise
     */
    static std::optional<ShardCertificateInfo> parseCertificate(const std::string& cert_path);
    
    /**
     * Parse certificate from PEM string
     * @param pem_data PEM-encoded certificate data
     * @return Certificate info if successful, nullopt otherwise
     */
    static std::optional<ShardCertificateInfo> parseCertificatePEM(const std::string& pem_data);
    
    /**
     * Verify certificate against Root CA
     * @param cert_path Path to certificate to verify
     * @param ca_cert_path Path to CA certificate
     * @return true if certificate is valid and trusted
     */
    static bool verifyCertificate(const std::string& cert_path, const std::string& ca_cert_path);
    
    /**
     * Check if certificate is in Certificate Revocation List
     * @param serial_number Certificate serial number
     * @param crl_path Path to CRL file (PEM)
     * @return true if certificate is revoked
     */
    static bool isRevoked(const std::string& serial_number, const std::string& crl_path);
    
    /**
     * Extract shard ID from certificate
     * Convenience method for quick shard ID lookup
     * @param cert_path Path to certificate
     * @return Shard ID if found, nullopt otherwise
     */
    static std::optional<std::string> getShardId(const std::string& cert_path);
    
    /**
     * Validate certificate for shard use
     * Checks:
     * - Certificate is not expired
     * - Has required shard_id extension
     * - Has at least one capability
     * - Token range is valid (start < end)
     * 
     * @param info Certificate info to validate
     * @return true if certificate is valid for shard use
     */
    static bool validateShardCertificate(const ShardCertificateInfo& info);

private:
    /**
     * Parse custom X.509 extensions
     * Internal helper to extract shard-specific extensions
     */
    static bool parseCustomExtensions(void* x509_cert, ShardCertificateInfo& info);
    
    /**
     * Parse Subject Alternative Names
     * Internal helper to extract SANs
     */
    static bool parseSAN(void* x509_cert, ShardCertificateInfo& info);
};

} // namespace themis::sharding
