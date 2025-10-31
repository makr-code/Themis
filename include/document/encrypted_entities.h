#pragma once

#include "security/encryption.h"
#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace themis {

/**
 * @brief User entity with encrypted PII fields
 * 
 * Demonstrates column-level encryption for personally identifiable information.
 * 
 * Encrypted Fields:
 * - email: Email address (searchable via deterministic encryption in future)
 * - phone: Phone number
 * - ssn: Social Security Number (high sensitivity)
 * - address: Full address string
 * 
 * Plain Fields:
 * - id: User identifier (UUID)
 * - username: Public username (not PII)
 * - created_at: Account creation timestamp
 * - status: Account status (active, suspended, deleted)
 * 
 * Example Usage:
 * @code
 * // Setup encryption
 * auto provider = std::make_shared<VaultKeyProvider>(...);
 * auto encryption = std::make_shared<FieldEncryption>(provider);
 * EncryptedField<std::string>::setFieldEncryption(encryption);
 * 
 * // Create user with encrypted data
 * User user;
 * user.id = "user-123";
 * user.username = "alice_smith";
 * user.email.encrypt("alice@example.com", "user_pii");
 * user.phone.encrypt("+1-555-0123", "user_pii");
 * user.ssn.encrypt("123-45-6789", "user_sensitive");
 * user.address.encrypt("123 Main St, NYC, NY 10001", "user_pii");
 * 
 * // Serialize to JSON (encrypted fields are base64-encoded)
 * json j = user.toJson();
 * db->put("user:user-123", j.dump());
 * 
 * // Deserialize and decrypt
 * User loaded = User::fromJson(j);
 * std::string email = loaded.email.decrypt();  // "alice@example.com"
 * @endcode
 * 
 * Key Management:
 * - user_pii: General PII (email, phone, address) - 1 year rotation
 * - user_sensitive: High-sensitivity data (SSN) - 6 month rotation
 * 
 * Compliance:
 * - GDPR: Right to be forgotten (delete user record)
 * - HIPAA: Encrypted PHI at rest
 * - PCI DSS: No credit card data stored (use tokenization instead)
 */
struct User {
    // Plain fields
    std::string id;                    // User UUID
    std::string username;              // Public username
    int64_t created_at;                // Unix timestamp (ms)
    std::string status;                // "active", "suspended", "deleted"
    
    // Encrypted fields (PII)
    EncryptedField<std::string> email;
    EncryptedField<std::string> phone;
    EncryptedField<std::string> ssn;
    EncryptedField<std::string> address;
    
    User() : created_at(0), status("active") {}
    
    /**
     * @brief Serialize to JSON
     * 
     * Encrypted fields are serialized as base64 strings with format:
     * "key_id:version:iv:ciphertext:tag"
     */
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["username"] = username;
        j["created_at"] = created_at;
        j["status"] = status;
        
        // Serialize encrypted fields (if encrypted)
        if (email.isEncrypted()) {
            j["email"] = email.toBase64();
        }
        if (phone.isEncrypted()) {
            j["phone"] = phone.toBase64();
        }
        if (ssn.isEncrypted()) {
            j["ssn"] = ssn.toBase64();
        }
        if (address.isEncrypted()) {
            j["address"] = address.toBase64();
        }
        
        return j;
    }
    
    /**
     * @brief Deserialize from JSON
     * 
     * Loads encrypted fields as-is (encrypted state).
     * Call decrypt() on individual fields to access plain values.
     */
    static User fromJson(const nlohmann::json& j) {
        User user;
        user.id = j.value("id", "");
        user.username = j.value("username", "");
        user.created_at = j.value("created_at", 0L);
        user.status = j.value("status", "active");
        
        // Deserialize encrypted fields
        if (j.contains("email")) {
            user.email = EncryptedField<std::string>::fromBase64(j["email"].get<std::string>());
        }
        if (j.contains("phone")) {
            user.phone = EncryptedField<std::string>::fromBase64(j["phone"].get<std::string>());
        }
        if (j.contains("ssn")) {
            user.ssn = EncryptedField<std::string>::fromBase64(j["ssn"].get<std::string>());
        }
        if (j.contains("address")) {
            user.address = EncryptedField<std::string>::fromBase64(j["address"].get<std::string>());
        }
        
        return user;
    }
};

/**
 * @brief Customer entity with financial data encryption
 * 
 * Demonstrates encryption for financial/healthcare applications.
 * 
 * Encrypted Fields:
 * - credit_score: Credit rating (sensitive financial info)
 * - annual_income: Income data (financial PII)
 * - medical_record_id: Healthcare record identifier (HIPAA)
 * 
 * Plain Fields:
 * - customer_id: Business identifier
 * - account_type: "personal", "business", "premium"
 * - risk_tier: Computed risk category (not PII)
 * 
 * Key Rotation Example:
 * @code
 * // Rotate key
 * uint32_t new_version = provider->rotateKey("customer_financial");
 * 
 * // Re-encrypt existing data
 * auto old_data = customer.annual_income.decrypt();
 * customer.annual_income.encrypt(old_data, "customer_financial");
 * @endcode
 */
struct Customer {
    // Plain fields
    std::string customer_id;
    std::string account_type;
    std::string risk_tier;
    int64_t created_at;
    
    // Encrypted fields (financial/healthcare)
    EncryptedField<int64_t> credit_score;    // 300-850 range
    EncryptedField<double> annual_income;     // USD amount
    EncryptedField<std::string> medical_record_id;
    
    Customer() : created_at(0), account_type("personal"), risk_tier("low") {}
    
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["customer_id"] = customer_id;
        j["account_type"] = account_type;
        j["risk_tier"] = risk_tier;
        j["created_at"] = created_at;
        
        if (credit_score.isEncrypted()) {
            j["credit_score"] = credit_score.toBase64();
        }
        if (annual_income.isEncrypted()) {
            j["annual_income"] = annual_income.toBase64();
        }
        if (medical_record_id.isEncrypted()) {
            j["medical_record_id"] = medical_record_id.toBase64();
        }
        
        return j;
    }
    
    static Customer fromJson(const nlohmann::json& j) {
        Customer customer;
        customer.customer_id = j.value("customer_id", "");
        customer.account_type = j.value("account_type", "personal");
        customer.risk_tier = j.value("risk_tier", "low");
        customer.created_at = j.value("created_at", 0L);
        
        if (j.contains("credit_score")) {
            customer.credit_score = EncryptedField<int64_t>::fromBase64(j["credit_score"].get<std::string>());
        }
        if (j.contains("annual_income")) {
            customer.annual_income = EncryptedField<double>::fromBase64(j["annual_income"].get<std::string>());
        }
        if (j.contains("medical_record_id")) {
            customer.medical_record_id = EncryptedField<std::string>::fromBase64(j["medical_record_id"].get<std::string>());
        }
        
        return customer;
    }
};

/**
 * @brief Enhanced DocumentMeta with encryption
 * 
 * Extends document metadata with encrypted content preview.
 * Useful for confidential documents where even metadata is sensitive.
 */
struct SecureDocument {
    std::string id;
    std::string title;
    int64_t created_at;
    
    // Encrypted fields
    EncryptedField<std::string> content_preview;  // First 500 chars
    EncryptedField<std::string> author;           // Document author
    EncryptedField<std::string> classification;   // "public", "confidential", "secret"
    
    SecureDocument() : created_at(0) {}
    
    nlohmann::json toJson() const {
        nlohmann::json j;
        j["id"] = id;
        j["title"] = title;
        j["created_at"] = created_at;
        
        if (content_preview.isEncrypted()) {
            j["content_preview"] = content_preview.toBase64();
        }
        if (author.isEncrypted()) {
            j["author"] = author.toBase64();
        }
        if (classification.isEncrypted()) {
            j["classification"] = classification.toBase64();
        }
        
        return j;
    }
    
    static SecureDocument fromJson(const nlohmann::json& j) {
        SecureDocument doc;
        doc.id = j.value("id", "");
        doc.title = j.value("title", "");
        doc.created_at = j.value("created_at", 0L);
        
        if (j.contains("content_preview")) {
            doc.content_preview = EncryptedField<std::string>::fromBase64(j["content_preview"].get<std::string>());
        }
        if (j.contains("author")) {
            doc.author = EncryptedField<std::string>::fromBase64(j["author"].get<std::string>());
        }
        if (j.contains("classification")) {
            doc.classification = EncryptedField<std::string>::fromBase64(j["classification"].get<std::string>());
        }
        
        return doc;
    }
};

} // namespace themis
