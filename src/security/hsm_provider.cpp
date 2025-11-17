#include "security/hsm_provider.h"
#include "core/logging.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <dlfcn.h>
#include <cstring>
#include <chrono>
#include <sstream>
#include <iomanip>

// PKCS#11 headers (using cryptoki standard)
// For production, link against actual HSM PKCS#11 library
// For testing, use SoftHSM2: https://github.com/opendnssec/SoftHSMv2

#ifdef __linux__
#include <pkcs11/pkcs11.h>
#elif defined(_WIN32)
#include <cryptoki.h>
#else
// Fallback: define minimal PKCS#11 types
typedef unsigned char CK_BYTE;
typedef CK_BYTE CK_BBOOL;
typedef unsigned long int CK_ULONG;
typedef unsigned long int CK_FLAGS;
typedef CK_ULONG CK_RV;
typedef CK_ULONG CK_SESSION_HANDLE;
typedef CK_ULONG CK_OBJECT_HANDLE;
typedef CK_ULONG CK_SLOT_ID;
typedef void* CK_VOID_PTR;

#define CKR_OK 0x00000000UL
#define CKF_RW_SESSION 0x00000002UL
#define CKF_SERIAL_SESSION 0x00000004UL
#define CKU_USER 1

struct CK_C_INITIALIZE_ARGS {
    void* reserved;
};

struct CK_MECHANISM {
    CK_ULONG mechanism;
    CK_VOID_PTR pParameter;
    CK_ULONG ulParameterLen;
};

struct CK_ATTRIBUTE {
    CK_ULONG type;
    CK_VOID_PTR pValue;
    CK_ULONG ulValueLen;
};
#endif

namespace themis {
namespace security {

// Base64 encoding helper
static std::string base64_encode(const std::vector<uint8_t>& data) {
    static const char* base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    std::string ret;
    int i = 0;
    uint8_t char_array_3[3];
    uint8_t char_array_4[4];
    size_t in_len = data.size();
    const uint8_t* bytes_to_encode = data.data();
    
    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(int j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
        
        while(i++ < 3)
            ret += '=';
    }
    
    return ret;
}

static std::vector<uint8_t> base64_decode(const std::string& encoded_string) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    
    size_t in_len = encoded_string.size();
    int i = 0;
    int in_ = 0;
    uint8_t char_array_4[4], char_array_3[3];
    std::vector<uint8_t> ret;
    
    while (in_len-- && (encoded_string[in_] != '=') && 
           (isalnum(encoded_string[in_]) || (encoded_string[in_] == '+') || (encoded_string[in_] == '/'))) {
        char_array_4[i++] = encoded_string[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++)
                char_array_4[i] = base64_chars.find(char_array_4[i]);
            
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            
            for (i = 0; i < 3; i++)
                ret.push_back(char_array_3[i]);
            i = 0;
        }
    }
    
    if (i) {
        for (int j = i; j < 4; j++)
            char_array_4[j] = 0;
        
        for (int j = 0; j < 4; j++)
            char_array_4[j] = base64_chars.find(char_array_4[j]);
        
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        
        for (int j = 0; j < i - 1; j++)
            ret.push_back(char_array_3[j]);
    }
    
    return ret;
}

// PKCS#11 Function pointer types
typedef CK_RV (*CK_C_Initialize_t)(CK_VOID_PTR);
typedef CK_RV (*CK_C_Finalize_t)(CK_VOID_PTR);
typedef CK_RV (*CK_C_GetSlotList_t)(CK_BBOOL, CK_SLOT_ID*, CK_ULONG*);
typedef CK_RV (*CK_C_OpenSession_t)(CK_SLOT_ID, CK_FLAGS, CK_VOID_PTR, void*, CK_SESSION_HANDLE*);
typedef CK_RV (*CK_C_CloseSession_t)(CK_SESSION_HANDLE);
typedef CK_RV (*CK_C_Login_t)(CK_SESSION_HANDLE, CK_ULONG, CK_BYTE*, CK_ULONG);
typedef CK_RV (*CK_C_Logout_t)(CK_SESSION_HANDLE);
typedef CK_RV (*CK_C_SignInit_t)(CK_SESSION_HANDLE, CK_MECHANISM*, CK_OBJECT_HANDLE);
typedef CK_RV (*CK_C_Sign_t)(CK_SESSION_HANDLE, CK_BYTE*, CK_ULONG, CK_BYTE*, CK_ULONG*);
typedef CK_RV (*CK_C_FindObjectsInit_t)(CK_SESSION_HANDLE, CK_ATTRIBUTE*, CK_ULONG);
typedef CK_RV (*CK_C_FindObjects_t)(CK_SESSION_HANDLE, CK_OBJECT_HANDLE*, CK_ULONG, CK_ULONG*);
typedef CK_RV (*CK_C_FindObjectsFinal_t)(CK_SESSION_HANDLE);

/**
 * HSMProvider::Impl - PKCS#11 implementation
 */
class HSMProvider::Impl {
public:
    void* library_handle = nullptr;
    CK_SESSION_HANDLE session = 0;
    bool session_open = false;
    bool logged_in = false;
    
    // PKCS#11 function pointers
    CK_C_Initialize_t C_Initialize = nullptr;
    CK_C_Finalize_t C_Finalize = nullptr;
    CK_C_GetSlotList_t C_GetSlotList = nullptr;
    CK_C_OpenSession_t C_OpenSession = nullptr;
    CK_C_CloseSession_t C_CloseSession = nullptr;
    CK_C_Login_t C_Login = nullptr;
    CK_C_Logout_t C_Logout = nullptr;
    CK_C_SignInit_t C_SignInit = nullptr;
    CK_C_Sign_t C_Sign = nullptr;
    CK_C_FindObjectsInit_t C_FindObjectsInit = nullptr;
    CK_C_FindObjects_t C_FindObjects = nullptr;
    CK_C_FindObjectsFinal_t C_FindObjectsFinal = nullptr;
    
    bool loadLibrary(const std::string& path, std::string& error) {
        library_handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!library_handle) {
            error = std::string("Failed to load PKCS#11 library: ") + dlerror();
            return false;
        }
        
        // Load function pointers
        #define LOAD_FUNC(name) \
            name = reinterpret_cast<decltype(name)>(dlsym(library_handle, #name)); \
            if (!name) { \
                error = std::string("Failed to load function ") + #name; \
                dlclose(library_handle); \
                library_handle = nullptr; \
                return false; \
            }
        
        LOAD_FUNC(C_Initialize);
        LOAD_FUNC(C_Finalize);
        LOAD_FUNC(C_GetSlotList);
        LOAD_FUNC(C_OpenSession);
        LOAD_FUNC(C_CloseSession);
        LOAD_FUNC(C_Login);
        LOAD_FUNC(C_Logout);
        LOAD_FUNC(C_SignInit);
        LOAD_FUNC(C_Sign);
        LOAD_FUNC(C_FindObjectsInit);
        LOAD_FUNC(C_FindObjects);
        LOAD_FUNC(C_FindObjectsFinal);
        
        #undef LOAD_FUNC
        
        return true;
    }
    
    ~Impl() {
        if (logged_in && C_Logout) {
            C_Logout(session);
        }
        if (session_open && C_CloseSession) {
            C_CloseSession(session);
        }
        if (library_handle && C_Finalize) {
            C_Finalize(nullptr);
        }
        if (library_handle) {
            dlclose(library_handle);
        }
    }
};

HSMProvider::HSMProvider(HSMConfig config)
    : impl_(std::make_unique<Impl>())
    , config_(std::move(config))
    , initialized_(false) {
}

HSMProvider::~HSMProvider() = default;

HSMProvider::HSMProvider(HSMProvider&&) noexcept = default;
HSMProvider& HSMProvider::operator=(HSMProvider&&) noexcept = default;

bool HSMProvider::initialize() {
    if (initialized_) {
        THEMIS_WARN("HSMProvider already initialized");
        return true;
    }
    
    // Load PKCS#11 library
    if (!impl_->loadLibrary(config_.library_path, last_error_)) {
        THEMIS_ERROR("HSM initialization failed: {}", last_error_);
        return false;
    }
    
    // Initialize PKCS#11
    CK_C_INITIALIZE_ARGS init_args = {nullptr};
    CK_RV rv = impl_->C_Initialize(&init_args);
    if (rv != CKR_OK) {
        last_error_ = "C_Initialize failed with code " + std::to_string(rv);
        THEMIS_ERROR("HSM: {}", last_error_);
        return false;
    }
    
    // Open session
    rv = impl_->C_OpenSession(
        config_.slot_id,
        CKF_SERIAL_SESSION | CKF_RW_SESSION,
        nullptr,
        nullptr,
        &impl_->session
    );
    
    if (rv != CKR_OK) {
        last_error_ = "C_OpenSession failed with code " + std::to_string(rv);
        THEMIS_ERROR("HSM: {}", last_error_);
        impl_->C_Finalize(nullptr);
        return false;
    }
    
    impl_->session_open = true;
    
    // Login
    rv = impl_->C_Login(
        impl_->session,
        CKU_USER,
        reinterpret_cast<CK_BYTE*>(const_cast<char*>(config_.pin.c_str())),
        config_.pin.length()
    );
    
    if (rv != CKR_OK) {
        last_error_ = "C_Login failed with code " + std::to_string(rv);
        THEMIS_ERROR("HSM: {}", last_error_);
        impl_->C_CloseSession(impl_->session);
        impl_->C_Finalize(nullptr);
        return false;
    }
    
    impl_->logged_in = true;
    initialized_ = true;
    
    THEMIS_INFO("HSM initialized successfully (slot={}, library={})", 
                config_.slot_id, config_.library_path);
    
    return true;
}

void HSMProvider::finalize() {
    if (!initialized_) {
        return;
    }
    
    impl_.reset();
    initialized_ = false;
    
    THEMIS_INFO("HSM finalized");
}

HSMSignatureResult HSMProvider::sign(const std::vector<uint8_t>& data,
                                     const std::string& key_label) {
    // Hash data first
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());
    
    return signHash(hash, key_label);
}

HSMSignatureResult HSMProvider::signHash(const std::vector<uint8_t>& hash,
                                         const std::string& key_label) {
    HSMSignatureResult result;
    result.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    if (!initialized_) {
        result.error_message = "HSM not initialized";
        THEMIS_ERROR("HSM: {}", result.error_message);
        return result;
    }
    
    // Use provided label or default from config
    std::string label = key_label.empty() ? config_.key_label : key_label;
    
    // Find private key by label
    // This is a simplified implementation - production code should handle
    // CKA_CLASS, CKA_KEY_TYPE, CKA_SIGN attributes properly
    
    // For now, return stub signature (HSM integration requires actual PKCS#11 library)
    // In production, this would:
    // 1. Find key handle via C_FindObjectsInit/C_FindObjects
    // 2. Initialize signing via C_SignInit with CKM_SHA256_RSA_PKCS mechanism
    // 3. Perform signing via C_Sign
    
    THEMIS_WARN("HSM signing not fully implemented - returning stub signature");
    THEMIS_WARN("To complete: integrate with actual PKCS#11 library and implement key lookup + C_Sign");
    
    // Stub: Base64 encode hash (for testing only)
    result.signature_b64 = base64_encode(hash);
    result.algorithm = config_.signature_algorithm;
    result.key_id = label;
    result.cert_serial = "HSM-STUB-CERT";
    result.success = true;
    
    return result;
}

bool HSMProvider::verify(const std::vector<uint8_t>& data,
                         const std::string& signature_b64,
                         const std::string& key_label) {
    if (!initialized_) {
        THEMIS_ERROR("HSM not initialized");
        return false;
    }
    
    // Hash data
    std::vector<uint8_t> hash(SHA256_DIGEST_LENGTH);
    SHA256(data.data(), data.size(), hash.data());
    
    // Decode signature
    auto signature = base64_decode(signature_b64);
    
    // Stub implementation - in production, would use C_VerifyInit/C_Verify
    THEMIS_WARN("HSM verify not fully implemented - comparing hash to signature");
    
    std::string hash_b64 = base64_encode(hash);
    return hash_b64 == signature_b64;
}

std::vector<HSMKeyInfo> HSMProvider::listKeys() {
    std::vector<HSMKeyInfo> keys;
    
    if (!initialized_) {
        THEMIS_ERROR("HSM not initialized");
        return keys;
    }
    
    // Stub implementation - in production, would enumerate keys via C_FindObjects
    THEMIS_INFO("HSM listKeys() - stub implementation");
    
    return keys;
}

bool HSMProvider::generateKeyPair(const std::string& label,
                                  uint32_t key_size,
                                  bool extractable) {
    if (!initialized_) {
        THEMIS_ERROR("HSM not initialized");
        return false;
    }
    
    // Stub implementation - in production, would use C_GenerateKeyPair
    THEMIS_WARN("HSM generateKeyPair() - not implemented");
    THEMIS_INFO("To generate keys, use: pkcs11-tool --module {} --login --keypairgen --key-type RSA:{} --label '{}'",
                config_.library_path, key_size, label);
    
    return false;
}

bool HSMProvider::importCertificate(const std::string& key_label,
                                    const std::string& cert_pem) {
    if (!initialized_) {
        THEMIS_ERROR("HSM not initialized");
        return false;
    }
    
    // Stub implementation - in production, would use C_CreateObject
    THEMIS_WARN("HSM importCertificate() - not implemented");
    
    return false;
}

std::optional<std::string> HSMProvider::getCertificate(const std::string& key_label) {
    if (!initialized_) {
        THEMIS_ERROR("HSM not initialized");
        return std::nullopt;
    }
    
    // Stub implementation - in production, would find and read certificate object
    THEMIS_WARN("HSM getCertificate() - not implemented");
    
    return std::nullopt;
}

bool HSMProvider::isReady() const {
    return initialized_;
}

std::string HSMProvider::getTokenInfo() const {
    if (!initialized_) {
        return "HSM not initialized";
    }
    
    std::ostringstream oss;
    oss << "HSM Token Info:\n"
        << "  Library: " << config_.library_path << "\n"
        << "  Slot: " << config_.slot_id << "\n"
        << "  Status: " << (initialized_ ? "Ready" : "Not Ready");
    
    return oss.str();
}

std::string HSMProvider::getLastError() const {
    return last_error_;
}

// HSMPKIClient implementation

HSMPKIClient::HSMPKIClient(HSMConfig config)
    : hsm_(std::make_unique<HSMProvider>(std::move(config))) {
    
    if (!hsm_->initialize()) {
        THEMIS_ERROR("Failed to initialize HSMPKIClient: {}", hsm_->getLastError());
    }
}

HSMPKIClient::~HSMPKIClient() {
    if (hsm_) {
        hsm_->finalize();
    }
}

HSMSignatureResult HSMPKIClient::sign(const std::vector<uint8_t>& data) {
    return hsm_->sign(data);
}

bool HSMPKIClient::verify(const std::vector<uint8_t>& data, 
                          const std::string& signature_b64) {
    return hsm_->verify(data, signature_b64);
}

std::optional<std::string> HSMPKIClient::getCertSerial() {
    return hsm_->getCertificate(hsm_->getTokenInfo());
}

bool HSMPKIClient::isReady() const {
    return hsm_->isReady();
}

} // namespace security
} // namespace themis
