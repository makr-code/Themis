#ifdef THEMIS_ENABLE_HSM_REAL
#include "security/hsm_provider.h"
#include "security/pkcs11_minimal.h"
#include "utils/logger.h"
#include <string>
#include <vector>
#include <optional>
#include <chrono>
#include <sstream>
#include <mutex>
#include <memory>
#include <atomic>
#include <cstring>
#include <openssl/evp.h>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace themis { namespace security {

// Real PKCS#11 implementation with graceful developer fallback.
// If any critical step fails (lib load, slot, login, key discovery),
// operations transparently revert to deterministic stub behaviour.

class PKCS11Loader {
public:
    bool load(const std::string& path) {
#if defined(_WIN32)
        lib_ = LoadLibraryA(path.c_str());
        if(!lib_) return false;
        auto getFn = (CK_C_GetFunctionList)GetProcAddress((HMODULE)lib_, "C_GetFunctionList");
        if(!getFn) return false;
        CK_RV rv = getFn(&funcs_);
        return rv == CKR_OK && funcs_ && funcs_->C_Initialize(nullptr) == CKR_OK;
#else
        lib_ = dlopen(path.c_str(), RTLD_NOW);
        if(!lib_) return false;
        auto getFn = (CK_C_GetFunctionList)dlsym(lib_, "C_GetFunctionList");
        if(!getFn) return false;
        CK_RV rv = getFn(&funcs_);
        return rv == CKR_OK && funcs_ && funcs_->C_Initialize(nullptr) == CKR_OK;
#endif
    }
    void unload() {
        if(funcs_) funcs_->C_Finalize(nullptr);
#if defined(_WIN32)
        if(lib_) FreeLibrary((HMODULE)lib_);
#else
        if(lib_) dlclose(lib_);
#endif
        lib_ = nullptr; funcs_ = nullptr;
    }
    CK_FUNCTION_LIST_PTR api() const { return funcs_; }
private:
    void* lib_ = nullptr;
    CK_FUNCTION_LIST_PTR funcs_ = nullptr;
};

// Base64 encoding using OpenSSL
static std::string toBase64(const std::vector<uint8_t>& data) {
    if(data.empty()) return "";
    // EVP_EncodeBlock adds null terminator and pads with '='
    size_t outLen = ((data.size() + 2) / 3) * 4;
    std::vector<unsigned char> encoded(outLen + 1);
    int len = EVP_EncodeBlock(encoded.data(), data.data(), (int)data.size());
    return std::string((char*)encoded.data(), len);
}

// Base64 decoding using OpenSSL
static std::vector<uint8_t> fromBase64(const std::string& b64) {
    if(b64.empty()) return {};
    size_t outLen = (b64.size() / 4) * 3;
    std::vector<unsigned char> decoded(outLen);
    int len = EVP_DecodeBlock(decoded.data(), (const unsigned char*)b64.data(), (int)b64.size());
    if(len < 0) return {}; // Decoding error
    // Remove padding bytes
    while(len > 0 && b64[b64.size() - (outLen - len)] == '=') --len;
    decoded.resize(len);
    return decoded;
}

class HSMProvider::Impl {
public:
    explicit Impl(HSMConfig cfg): config(cfg), next_session_idx(0) {}
    HSMConfig config;

    PKCS11Loader loader;
    struct SessionEntry {
        CK_SESSION_HANDLE handle = 0;
        CK_OBJECT_HANDLE privKey = 0;
        CK_OBJECT_HANDLE pubKey = 0;
        CK_OBJECT_HANDLE certObj = 0;
        bool ready = false;
    };
    std::vector<SessionEntry> pool;
    bool real_ready = false; // true wenn mind. eine Session mit privKey
    std::mutex mtx;
    std::string cert_serial_cache_;
    std::atomic<uint32_t> next_session_idx; // Lock-free round-robin counter

    // Performance metrics
    std::atomic<uint64_t> sign_count{0};
    std::atomic<uint64_t> verify_count{0};
    std::atomic<uint64_t> sign_errors{0};
    std::atomic<uint64_t> verify_errors{0};
    std::atomic<uint64_t> total_sign_time_us{0};
    std::atomic<uint64_t> total_verify_time_us{0};
    std::atomic<uint64_t> pool_round_robin_hits{0};

    void fallbackLogOnce(const std::string& reason){
        if(!real_ready){
            THEMIS_WARN("HSMProvider PKCS#11 fallback aktiv – {}", reason);
        }
    }
};

static std::string mapError(CK_RV rv){
    switch(rv){
        case CKR_OK: return "OK";
        case CKR_PIN_INCORRECT: return "PIN incorrect";
        case CKR_DEVICE_ERROR: return "Device error";
        case CKR_GENERAL_ERROR: return "General error";
        case CKR_ARGUMENTS_BAD: return "Bad arguments";
        case CKR_SIGNATURE_INVALID: return "Signature invalid";
        default: {
            std::ostringstream oss; oss << "CKR_0x" << std::hex << rv; return oss.str();
        }
    }
}

HSMProvider::HSMProvider(HSMConfig config)
    : impl_(std::make_unique<Impl>(config)), config_(config) {}
HSMProvider::~HSMProvider(){ finalize(); }
HSMProvider::HSMProvider(HSMProvider&&) noexcept = default;
HSMProvider& HSMProvider::operator=(HSMProvider&&) noexcept = default;

bool HSMProvider::initialize(){
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if(initialized_) return true;
    // Attempt real PKCS#11
    if(!config_.library_path.empty() && impl_->loader.load(config_.library_path)){
        auto api = impl_->loader.api();
        if(!api){ impl_->fallbackLogOnce("Function list leer"); }
        else {
            // Slot list
            uint32_t slotCount = 0; CK_RV rv = api->C_GetSlotList(1, nullptr, &slotCount);
            if(rv == CKR_OK && slotCount){
                std::vector<CK_SLOT_ID> slots(slotCount);
                rv = api->C_GetSlotList(1, slots.data(), &slotCount);
                if(rv == CKR_OK){
                    CK_SLOT_ID chosen = slots[0];
                    if(api->C_OpenSession(chosen, CKF_SERIAL_SESSION, nullptr, nullptr, &impl_->session) == CKR_OK){
                        std::string pin = config_.pin;
                        if(pin.empty()){ const char* envPin = std::getenv("THEMIS_HSM_PIN"); if(envPin) pin = envPin; }
                        if(!pin.empty()){
                            rv = api->C_Login(impl_->session, CKU_USER, (CK_BYTE_PTR)pin.data(), (uint32_t)pin.size());
                            if(rv == CKR_OK){ 
                                // Discover private/public key by label (best effort)
                                // Session-Pool erstellen
                                uint32_t poolSize = config_.session_pool_size;
                                if(const char* envPool = std::getenv("THEMIS_HSM_SESSION_POOL")){
                                    poolSize = std::max(1u, (uint32_t)std::atoi(envPool));
                                }
                                impl_->pool.resize(poolSize);
                                for(uint32_t i=0;i<poolSize;++i){
                                    if(api->C_OpenSession(chosen, CKF_SERIAL_SESSION, nullptr, nullptr, &impl_->pool[i].handle) != CKR_OK){
                                        impl_->fallbackLogOnce("OpenSession im Pool fehlgeschlagen");
                                        continue;
                                    }
                                    // Login pro Session (einige HSMs verlangen das)
                                    if(!pin.empty()){
                                        CK_RV rvLogin = api->C_Login(impl_->pool[i].handle, CKU_USER, (CK_BYTE_PTR)pin.data(), (uint32_t)pin.size());
                                        if(rvLogin != CKR_OK && rvLogin != CKR_PIN_INCORRECT){
                                            impl_->fallbackLogOnce("Login in Session fehlgeschlagen");
                                            continue;
                                        }
                                    }
                                    discoverKeysSession(impl_->pool[i]);
                                    discoverCertificateSession(impl_->pool[i]);
                                    impl_->pool[i].ready = (impl_->pool[i].privKey != 0);
                                }
                                // Serial einmalig setzen (erste gefundene Zert-Session)
                                for(auto& s : impl_->pool){ if(s.certObj){ impl_->real_ready = s.ready; break; } }
                                if(!impl_->real_ready){
                                    // Falls kein privKey entdeckt, dennoch fallback aktiv
                                    impl_->fallbackLogOnce("Kein Private Key im Pool gefunden – Fallback");
                                }
                                if(!impl_->real_ready) impl_->fallbackLogOnce("Kein Private Key gefunden – Fallback aktiv");
                            }
                            else { last_error_ = mapError(rv); impl_->fallbackLogOnce("Login fehlgeschlagen"); }
                        } else {
                            impl_->fallbackLogOnce("PIN leer – Login uebersprungen");
                        }
                    } else impl_->fallbackLogOnce("Session Open fehlgeschlagen");
                } else impl_->fallbackLogOnce("SlotList Abruf fehlgeschlagen");
            } else impl_->fallbackLogOnce("Keine Slots gefunden");
        }
    } else {
        impl_->fallbackLogOnce("Bibliothek konnte nicht geladen werden");
    }
    initialized_ = true;
    THEMIS_INFO("HSMProvider init (real_ready={})", impl_->real_ready?"true":"false");
    return true;
}

void HSMProvider::finalize(){
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if(!initialized_) return;
    if(impl_->real_ready && impl_->loader.api()){
        auto api = impl_->loader.api();
        if(impl_->session) { api->C_Logout(impl_->session); api->C_CloseSession(impl_->session); }
        impl_->loader.unload();
    }
    impl_->real_ready = false;
    initialized_ = false;
}

static uint64_t nowMs(){ return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

// Compute SHA-256 digest using OpenSSL EVP
static std::vector<uint8_t> sha256(const std::vector<uint8_t>& data){
    std::vector<uint8_t> out(EVP_MAX_MD_SIZE); unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, out.data(), &len);
    EVP_MD_CTX_free(ctx);
    out.resize(len);
    return out;
}

// DER prefix for SHA-256 DigestInfo (for CKM_RSA_PKCS when not using CKM_SHA256_RSA_PKCS)
static const uint8_t SHA256_DER_PREFIX[] = {
    0x30,0x31,0x30,0x0d,0x06,0x09,0x60,0x86,0x48,0x01,0x65,0x03,0x04,0x02,0x01,0x05,0x00,0x04,0x20
};

// Append DER prefix + digest for raw RSA PKCS#1v1.5 signing
static std::vector<uint8_t> makeDigestInfo(const std::vector<uint8_t>& digest){
    std::vector<uint8_t> di(sizeof(SHA256_DER_PREFIX) + digest.size());
    std::memcpy(di.data(), SHA256_DER_PREFIX, sizeof(SHA256_DER_PREFIX));
    std::memcpy(di.data()+sizeof(SHA256_DER_PREFIX), digest.data(), digest.size());
    return di;
}

// Key discovery helper
void HSMProvider::discoverKeysSession(SessionEntry& s){
    auto api = impl_->loader.api(); if(!api || !s.handle) return;
    std::string label = config_.key_label;
    CK_ATTRIBUTE privTemplate[2]; uint32_t clsPriv = CKO_PRIVATE_KEY;
    privTemplate[0].type = CKA_CLASS; privTemplate[0].pValue=&clsPriv; privTemplate[0].ulValueLen=sizeof(clsPriv);
    privTemplate[1].type = CKA_LABEL; privTemplate[1].pValue=(void*)label.c_str(); privTemplate[1].ulValueLen=label.size();
    if(api->C_FindObjectsInit(s.handle, privTemplate, 2)==CKR_OK){
        CK_OBJECT_HANDLE h; uint32_t found=0; if(api->C_FindObjects(s.handle,&h,1,&found)==CKR_OK && found==1) s.privKey=h; api->C_FindObjectsFinal(s.handle);
    }
    CK_ATTRIBUTE pubTemplate[2]; uint32_t clsPub = CKO_PUBLIC_KEY;
    pubTemplate[0].type = CKA_CLASS; pubTemplate[0].pValue=&clsPub; pubTemplate[0].ulValueLen=sizeof(clsPub);
    pubTemplate[1].type = CKA_LABEL; pubTemplate[1].pValue=(void*)label.c_str(); pubTemplate[1].ulValueLen=label.size();
    if(api->C_FindObjectsInit(s.handle, pubTemplate, 2)==CKR_OK){
        CK_OBJECT_HANDLE h; uint32_t found=0; if(api->C_FindObjects(s.handle,&h,1,&found)==CKR_OK && found==1) s.pubKey=h; api->C_FindObjectsFinal(s.handle);
    }
}

void HSMProvider::discoverCertificateSession(SessionEntry& s){
    auto api = impl_->loader.api(); if(!api || !s.handle) return;
    std::string label = config_.key_label;
    CK_ATTRIBUTE certTemplate[2]; uint32_t clsCert = CKO_CERTIFICATE;
    certTemplate[0].type = CKA_CLASS; certTemplate[0].pValue=&clsCert; certTemplate[0].ulValueLen=sizeof(clsCert);
    certTemplate[1].type = CKA_LABEL; certTemplate[1].pValue=(void*)label.c_str(); certTemplate[1].ulValueLen=label.size();
    if(api->C_FindObjectsInit(s.handle, certTemplate, 2)==CKR_OK){
        CK_OBJECT_HANDLE h; uint32_t found=0; if(api->C_FindObjects(s.handle,&h,1,&found)==CKR_OK && found==1) s.certObj=h; api->C_FindObjectsFinal(s.handle);
    }
    if(s.certObj && api->C_GetAttributeValue && cert_serial_cache_.empty()){
        CK_ATTRIBUTE valAttr; valAttr.type=CKA_VALUE; valAttr.pValue=nullptr; valAttr.ulValueLen=0;
        if(api->C_GetAttributeValue(s.handle, s.certObj, &valAttr, 1)==CKR_OK && valAttr.ulValueLen>0){
            std::vector<unsigned char> der(valAttr.ulValueLen); valAttr.pValue=der.data();
            if(api->C_GetAttributeValue(s.handle, s.certObj, &valAttr, 1)==CKR_OK){
                const unsigned char* p = der.data(); X509* x = d2i_X509(nullptr,&p,der.size());
                if(x){ ASN1_INTEGER* si = X509_get_serialNumber(x); if(si){ BIGNUM* bn=ASN1_INTEGER_to_BN(si,nullptr); if(bn){ char* hex=BN_bn2hex(bn); if(hex){ cert_serial_cache_ = hex; OPENSSL_free(hex);} BN_free(bn);} }
                    X509_free(x); }
            }
        }
    }
}

HSMProvider::SessionEntry* HSMProvider::acquireSession(){
    // Lock-free round-robin selection: find next ready session
    uint32_t poolSize = impl_->pool.size();
    if(poolSize == 0) return nullptr;
    
    // Try up to poolSize iterations to find ready session
    for(uint32_t attempt = 0; attempt < poolSize; ++attempt){
        uint32_t idx = impl_->next_session_idx.fetch_add(1, std::memory_order_relaxed) % poolSize;
        if(impl_->pool[idx].ready){
            impl_->pool_round_robin_hits.fetch_add(1, std::memory_order_relaxed);
            return &impl_->pool[idx];
        }
    }
    // Fallback: return first ready session if round-robin failed
    for(auto& s: impl_->pool){ if(s.ready) return &s; }
    return nullptr;
}

void HSMProvider::releaseSession(SessionEntry* s){ 
    // No-op for lock-free implementation (no busy flag to clear)
    (void)s;
}

HSMSignatureResult HSMProvider::sign(const std::vector<uint8_t>& data, const std::string& key_label){
    // Hash first (SHA-256) then sign
    auto digest = sha256(data);
    return signHash(digest, key_label);
}

HSMSignatureResult HSMProvider::signHash(const std::vector<uint8_t>& hash, const std::string& key_label){
    auto startTime = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::mutex> lock(impl_->mtx);
    HSMSignatureResult r;
    if(!initialized_){ 
        r.error_message = "Nicht initialisiert"; 
        impl_->sign_errors.fetch_add(1, std::memory_order_relaxed);
        return r; 
    }
    if(!impl_->real_ready){
        // Fallback stub behaviour: return Base64-encoded hash
        r.success = true; 
        r.signature_b64 = toBase64(hash);
        r.algorithm = config_.signature_algorithm; 
        r.key_id = key_label.empty()?config_.key_label:key_label; 
        r.cert_serial = "STUB-CERT"; 
        r.timestamp_ms = nowMs();
        impl_->sign_count.fetch_add(1, std::memory_order_relaxed);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        impl_->total_sign_time_us.fetch_add(elapsed, std::memory_order_relaxed);
        return r;
    }
    auto api = impl_->loader.api();
    if(!api){ 
        r.error_message = "PKCS#11 API null"; 
        impl_->sign_errors.fetch_add(1, std::memory_order_relaxed);
        return r; 
    }
    auto sess = acquireSession();
    if(!sess || sess->privKey==0){ 
        r.error_message = "PrivKey nicht gefunden"; 
        impl_->sign_errors.fetch_add(1, std::memory_order_relaxed);
        return r; 
    }
    CK_MECHANISM mech{};
    // If algorithm hints SHA256_RSA use combined mechanism else raw PKCS#1 with DigestInfo wrapper
    bool useCombined = (config_.signature_algorithm.find("SHA256") != std::string::npos);
    mech.mechanism = useCombined ? CKM_SHA256_RSA_PKCS : CKM_RSA_PKCS;
    CK_RV rv = api->C_SignInit(sess->handle, &mech, sess->privKey);
    if(rv != CKR_OK){ 
        r.error_message = mapError(rv); 
        impl_->sign_errors.fetch_add(1, std::memory_order_relaxed);
        return r; 
    }
    std::vector<uint8_t> input;
    if(useCombined){ input = hash; }
    else { input = makeDigestInfo(hash); }
    uint32_t sigLen = 4096; std::vector<CK_BYTE> sig(sigLen);
    rv = api->C_Sign(sess->handle, (CK_BYTE_PTR)input.data(), (uint32_t)input.size(), sig.data(), &sigLen);
    if(rv != CKR_OK){ 
        r.error_message = mapError(rv); 
        impl_->sign_errors.fetch_add(1, std::memory_order_relaxed);
        return r; 
    }
    sig.resize(sigLen);
    r.success = true; 
    impl_->sign_count.fetch_add(1, std::memory_order_relaxed);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - startTime).count();
    impl_->total_sign_time_us.fetch_add(elapsed, std::memory_order_relaxed);
    r.signature_b64 = toBase64(std::vector<uint8_t>(sig.begin(), sig.end()));
    r.algorithm = config_.signature_algorithm; 
    r.key_id = key_label.empty()?config_.key_label:key_label; 
    r.cert_serial = cert_serial_cache_.empty()?"REAL-CERT":cert_serial_cache_; 
    r.timestamp_ms = nowMs();
    releaseSession(sess);
    return r;
}

bool HSMProvider::verify(const std::vector<uint8_t>& data, const std::string& signature_b64, const std::string& key_label){
    auto startTime = std::chrono::high_resolution_clock::now();
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if(!initialized_) {
        impl_->verify_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if(!impl_->real_ready){
        // Fallback: verify by comparing Base64-encoded hash
        auto expected = toBase64(sha256(data));
        bool result = (expected == signature_b64);
        if(result) impl_->verify_count.fetch_add(1, std::memory_order_relaxed);
        else impl_->verify_errors.fetch_add(1, std::memory_order_relaxed);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        impl_->total_verify_time_us.fetch_add(elapsed, std::memory_order_relaxed);
        return result;
    }
    auto sess = acquireSession();
    auto api = impl_->loader.api(); 
    if(!api || !sess || sess->pubKey==0) { 
        impl_->verify_errors.fetch_add(1, std::memory_order_relaxed);
        releaseSession(sess); 
        return false; 
    }
    auto digest = sha256(data);
    bool useCombined = (config_.signature_algorithm.find("SHA256") != std::string::npos);
    std::vector<uint8_t> input = useCombined ? digest : makeDigestInfo(digest);
    // Decode Base64 signature
    std::vector<uint8_t> sig = fromBase64(signature_b64);
    if(sig.empty()) { 
        impl_->verify_errors.fetch_add(1, std::memory_order_relaxed);
        releaseSession(sess); 
        return false; 
    }
    
    CK_MECHANISM mech{}; mech.mechanism = useCombined ? CKM_SHA256_RSA_PKCS : CKM_RSA_PKCS;
    if(api->C_VerifyInit(sess->handle, &mech, sess->pubKey) != CKR_OK){ 
        impl_->verify_errors.fetch_add(1, std::memory_order_relaxed);
        releaseSession(sess); 
        return false; 
    }
    CK_RV rv = api->C_Verify(sess->handle, (CK_BYTE_PTR)input.data(), (uint32_t)input.size(), (CK_BYTE_PTR)sig.data(), (uint32_t)sig.size());
    bool result = (rv == CKR_OK);
    if(result) impl_->verify_count.fetch_add(1, std::memory_order_relaxed);
    else impl_->verify_errors.fetch_add(1, std::memory_order_relaxed);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - startTime).count();
    impl_->total_verify_time_us.fetch_add(elapsed, std::memory_order_relaxed);
    releaseSession(sess);
    return result;
}

std::vector<HSMKeyInfo> HSMProvider::listKeys(){
    std::lock_guard<std::mutex> lock(impl_->mtx);
    HSMKeyInfo info; info.label = config_.key_label; info.id = impl_->real_ready?"real-id":"stub-id"; info.algorithm = config_.signature_algorithm; info.can_sign = true; info.can_verify = true; info.extractable = false; info.key_size = impl_->real_ready?2048:0; return {info};
}

bool HSMProvider::generateKeyPair(const std::string& label, uint32_t key_size, bool extractable){
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if(!impl_->real_ready){ THEMIS_WARN("generateKeyPair Fallback stub (label='{}')", label); return false; }
    // Key generation omitted (would use C_GenerateKeyPair)
    THEMIS_WARN("generateKeyPair reale Implementierung noch nicht vorhanden");
    return false;
}

bool HSMProvider::importCertificate(const std::string& key_label, const std::string& cert_pem){
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if(!impl_->real_ready){ THEMIS_WARN("importCertificate Fallback stub (key='{}')", key_label); return false; }
    // Would create certificate object; store serial
    return false;
}

std::optional<std::string> HSMProvider::getCertificate(const std::string& key_label){
    std::lock_guard<std::mutex> lock(impl_->mtx);
    if(!impl_->real_ready) return std::string("-----BEGIN CERTIFICATE-----\nSTUB\n-----END CERTIFICATE-----\n");
    auto api = impl_->loader.api(); if(!api || impl_->certObj==0 || !api->C_GetAttributeValue) return std::nullopt;
    CK_ATTRIBUTE valAttr; valAttr.type = CKA_VALUE; valAttr.pValue = nullptr; valAttr.ulValueLen = 0;
    // Zertifikat aus erster Session mit certObj
    HSMProvider::SessionEntry* sess = nullptr; for(auto& s: impl_->pool){ if(s.certObj){ sess=&s; break; } }
    if(!sess) return std::nullopt;
    if(api->C_GetAttributeValue(sess->handle, sess->certObj, &valAttr, 1) != CKR_OK || valAttr.ulValueLen==0) return std::nullopt;
    std::vector<unsigned char> der(valAttr.ulValueLen); valAttr.pValue = der.data();
    if(api->C_GetAttributeValue(sess->handle, sess->certObj, &valAttr, 1) != CKR_OK) return std::nullopt;
    const unsigned char* p = der.data(); X509* x = d2i_X509(nullptr, &p, der.size()); if(!x) return std::nullopt;
    BIO* mem = BIO_new(BIO_s_mem());
    PEM_write_bio_X509(mem, x);
    X509_free(x);
    char* buf = nullptr; long len = BIO_get_mem_data(mem, &buf);
    std::string pem(buf, len);
    BIO_free(mem);
    return pem;
}

bool HSMProvider::isReady() const { return impl_->real_ready || initialized_; }

std::string HSMProvider::getTokenInfo() const { return impl_->real_ready?"PKCS11 real session active":"PKCS11 fallback stub"; }

std::string HSMProvider::getLastError() const { return last_error_; }

HSMPerformanceStats HSMProvider::getStats() const {
    HSMPerformanceStats stats;
    stats.sign_count = impl_->sign_count.load(std::memory_order_relaxed);
    stats.verify_count = impl_->verify_count.load(std::memory_order_relaxed);
    stats.sign_errors = impl_->sign_errors.load(std::memory_order_relaxed);
    stats.verify_errors = impl_->verify_errors.load(std::memory_order_relaxed);
    stats.total_sign_time_us = impl_->total_sign_time_us.load(std::memory_order_relaxed);
    stats.total_verify_time_us = impl_->total_verify_time_us.load(std::memory_order_relaxed);
    stats.pool_size = impl_->pool.size();
    stats.pool_round_robin_hits = impl_->pool_round_robin_hits.load(std::memory_order_relaxed);
    return stats;
}

void HSMProvider::resetStats() {
    impl_->sign_count.store(0, std::memory_order_relaxed);
    impl_->verify_count.store(0, std::memory_order_relaxed);
    impl_->sign_errors.store(0, std::memory_order_relaxed);
    impl_->verify_errors.store(0, std::memory_order_relaxed);
    impl_->total_sign_time_us.store(0, std::memory_order_relaxed);
    impl_->total_verify_time_us.store(0, std::memory_order_relaxed);
    impl_->pool_round_robin_hits.store(0, std::memory_order_relaxed);
}

} } // namespace themis::security
#endif // THEMIS_ENABLE_HSM_REAL
