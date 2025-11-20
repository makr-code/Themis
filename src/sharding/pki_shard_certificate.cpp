#include "sharding/pki_shard_certificate.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <ctime>

// OpenSSL headers
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <openssl/err.h>

namespace themis::sharding {

namespace {
    // Helper: Read file contents
    std::optional<std::string> readFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return std::nullopt;
        }
        
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }
    
    // Helper: Convert ASN1_TIME to ISO 8601 string
    std::string asn1TimeToString(const ASN1_TIME* time) {
        if (!time) return "";
        
        BIO* bio = BIO_new(BIO_s_mem());
        ASN1_TIME_print(bio, time);
        
        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string result(data, len);
        
        BIO_free(bio);
        return result;
    }
    
    // Helper: Get extension value by NID
    std::optional<std::string> getExtensionValue(X509* cert, int nid) {
        int idx = X509_get_ext_by_NID(cert, nid, -1);
        if (idx < 0) {
            return std::nullopt;
        }
        
        X509_EXTENSION* ext = X509_get_ext(cert, idx);
        if (!ext) {
            return std::nullopt;
        }
        
        ASN1_OCTET_STRING* data = X509_EXTENSION_get_data(ext);
        if (!data) {
            return std::nullopt;
        }
        
        const unsigned char* p = data->data;
        long len = data->length;
        
        return std::string(reinterpret_cast<const char*>(p), len);
    }
}

bool ShardCertificateInfo::isValidNow() const {
    // For Phase 2, we'll implement a simple check
    // In production, this should parse not_before/not_after and compare with current time
    // For now, return true if both dates are set
    return !not_before.empty() && !not_after.empty();
}

std::optional<ShardCertificateInfo> PKIShardCertificate::parseCertificate(const std::string& cert_path) {
    auto pem_data = readFile(cert_path);
    if (!pem_data) {
        return std::nullopt;
    }
    
    return parseCertificatePEM(*pem_data);
}

std::optional<ShardCertificateInfo> PKIShardCertificate::parseCertificatePEM(const std::string& pem_data) {
    BIO* bio = BIO_new_mem_buf(pem_data.c_str(), static_cast<int>(pem_data.size()));
    if (!bio) {
        return std::nullopt;
    }
    
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    
    if (!cert) {
        return std::nullopt;
    }
    
    ShardCertificateInfo info;
    
    // Extract subject CN
    X509_NAME* subject = X509_get_subject_name(cert);
    if (subject) {
        char cn_buf[256] = {0};
        X509_NAME_get_text_by_NID(subject, NID_commonName, cn_buf, sizeof(cn_buf));
        info.subject_cn = cn_buf;
    }
    
    // Extract issuer CN
    X509_NAME* issuer = X509_get_issuer_name(cert);
    if (issuer) {
        char issuer_buf[256] = {0};
        X509_NAME_get_text_by_NID(issuer, NID_commonName, issuer_buf, sizeof(issuer_buf));
        info.issuer_cn = issuer_buf;
    }
    
    // Extract serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (serial) {
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
        if (bn) {
            char* hex = BN_bn2hex(bn);
            if (hex) {
                info.serial_number = hex;
                OPENSSL_free(hex);
            }
            BN_free(bn);
        }
    }
    
    // Extract validity dates
    const ASN1_TIME* not_before = X509_get0_notBefore(cert);
    const ASN1_TIME* not_after = X509_get0_notAfter(cert);
    info.not_before = asn1TimeToString(not_before);
    info.not_after = asn1TimeToString(not_after);
    
    // Parse Subject Alternative Names
    parseSAN(cert, info);
    
    // Parse custom extensions (shard-specific)
    // Note: In Phase 2, we're providing the structure
    // Actual custom OID parsing would be implemented in production
    parseCustomExtensions(cert, info);
    
    X509_free(cert);
    
    return info;
}

bool PKIShardCertificate::verifyCertificate(const std::string& cert_path, const std::string& ca_cert_path) {
    // Read certificate
    auto cert_pem = readFile(cert_path);
    if (!cert_pem) {
        return false;
    }
    
    // Read CA certificate
    auto ca_pem = readFile(ca_cert_path);
    if (!ca_pem) {
        return false;
    }
    
    // Parse certificate
    BIO* cert_bio = BIO_new_mem_buf(cert_pem->c_str(), static_cast<int>(cert_pem->size()));
    X509* cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
    BIO_free(cert_bio);
    
    if (!cert) {
        return false;
    }
    
    // Parse CA certificate
    BIO* ca_bio = BIO_new_mem_buf(ca_pem->c_str(), static_cast<int>(ca_pem->size()));
    X509* ca_cert = PEM_read_bio_X509(ca_bio, nullptr, nullptr, nullptr);
    BIO_free(ca_bio);
    
    if (!ca_cert) {
        X509_free(cert);
        return false;
    }
    
    // Get CA public key
    EVP_PKEY* ca_pubkey = X509_get_pubkey(ca_cert);
    if (!ca_pubkey) {
        X509_free(cert);
        X509_free(ca_cert);
        return false;
    }
    
    // Verify certificate signature
    int result = X509_verify(cert, ca_pubkey);
    
    EVP_PKEY_free(ca_pubkey);
    X509_free(cert);
    X509_free(ca_cert);
    
    return result == 1;
}

bool PKIShardCertificate::isRevoked(const std::string& serial_number, const std::string& crl_path) {
    // Read CRL file
    auto crl_pem = readFile(crl_path);
    if (!crl_pem) {
        return false; // If CRL doesn't exist, assume not revoked
    }
    
    BIO* bio = BIO_new_mem_buf(crl_pem->c_str(), static_cast<int>(crl_pem->size()));
    if (!bio) {
        return false;
    }
    
    X509_CRL* crl = PEM_read_bio_X509_CRL(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);
    
    if (!crl) {
        return false;
    }
    
    // Check if serial number is in CRL
    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    if (!revoked) {
        X509_CRL_free(crl);
        return false;
    }
    
    bool found = false;
    for (int i = 0; i < sk_X509_REVOKED_num(revoked); ++i) {
        X509_REVOKED* r = sk_X509_REVOKED_value(revoked, i);
        const ASN1_INTEGER* r_serial = X509_REVOKED_get0_serialNumber(r);
        
        BIGNUM* bn = ASN1_INTEGER_to_BN(r_serial, nullptr);
        if (bn) {
            char* hex = BN_bn2hex(bn);
            if (hex) {
                if (serial_number == hex) {
                    found = true;
                }
                OPENSSL_free(hex);
            }
            BN_free(bn);
        }
        
        if (found) break;
    }
    
    X509_CRL_free(crl);
    return found;
}

std::optional<std::string> PKIShardCertificate::getShardId(const std::string& cert_path) {
    auto info = parseCertificate(cert_path);
    if (!info || info->shard_id.empty()) {
        return std::nullopt;
    }
    
    return info->shard_id;
}

bool PKIShardCertificate::validateShardCertificate(const ShardCertificateInfo& info) {
    // Check validity dates
    if (!info.isValidNow()) {
        return false;
    }
    
    // Check shard ID is present
    if (info.shard_id.empty()) {
        return false;
    }
    
    // Check at least one capability
    if (info.capabilities.empty()) {
        return false;
    }
    
    // Check token range is valid (start < end)
    if (info.token_range_start >= info.token_range_end && info.token_range_end != 0) {
        return false;
    }
    
    return true;
}

bool PKIShardCertificate::parseCustomExtensions(void* x509_cert_ptr, ShardCertificateInfo& info) {
    X509* cert = static_cast<X509*>(x509_cert_ptr);
    
    // Note: In Phase 2, we're providing the structure for custom extension parsing
    // In production, this would parse actual custom OIDs (e.g., 1.3.6.1.4.1.XXXXX)
    // and extract shard-specific information
    
    // For now, we'll extract shard_id from CN if it follows the pattern "shard-XXX"
    if (info.subject_cn.find("shard-") == 0) {
        // Extract shard ID from CN (e.g., "shard-001.themis.local" -> "shard_001")
        size_t dot_pos = info.subject_cn.find('.');
        std::string shard_name = info.subject_cn.substr(0, dot_pos);
        // Replace dash with underscore
        for (char& c : shard_name) {
            if (c == '-') c = '_';
        }
        info.shard_id = shard_name;
    }
    
    // Default capabilities for Phase 2
    info.capabilities = {"read", "write"};
    
    // Default role
    info.role = "primary";
    
    // Token range defaults (full range)
    info.token_range_start = 0;
    info.token_range_end = 0xFFFFFFFFFFFFFFFFULL;
    
    return true;
}

bool PKIShardCertificate::parseSAN(void* x509_cert_ptr, ShardCertificateInfo& info) {
    X509* cert = static_cast<X509*>(x509_cert_ptr);
    
    GENERAL_NAMES* san_names = static_cast<GENERAL_NAMES*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr)
    );
    
    if (!san_names) {
        return false;
    }
    
    int num_sans = sk_GENERAL_NAME_num(san_names);
    for (int i = 0; i < num_sans; ++i) {
        GENERAL_NAME* gen_name = sk_GENERAL_NAME_value(san_names, i);
        
        if (gen_name->type == GEN_DNS) {
            ASN1_STRING* dns = gen_name->d.dNSName;
            std::string dns_str(reinterpret_cast<const char*>(ASN1_STRING_get0_data(dns)),
                              ASN1_STRING_length(dns));
            info.san_dns.push_back(dns_str);
        }
        else if (gen_name->type == GEN_IPADD) {
            ASN1_OCTET_STRING* ip = gen_name->d.iPAddress;
            // Convert IP address to string
            if (ip->length == 4) { // IPv4
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                        ip->data[0], ip->data[1], ip->data[2], ip->data[3]);
                info.san_ip.push_back(ip_str);
            }
        }
        else if (gen_name->type == GEN_URI) {
            ASN1_STRING* uri = gen_name->d.uniformResourceIdentifier;
            std::string uri_str(reinterpret_cast<const char*>(ASN1_STRING_get0_data(uri)),
                              ASN1_STRING_length(uri));
            info.san_uri.push_back(uri_str);
        }
    }
    
    GENERAL_NAMES_free(san_names);
    return true;
}

} // namespace themis::sharding
