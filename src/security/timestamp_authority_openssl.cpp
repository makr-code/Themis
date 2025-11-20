#ifdef THEMIS_USE_OPENSSL_TSA
// OpenSSL/CURL based TimestampAuthority implementation (RFC 3161)
// Separate from stub to avoid dependency bloat when not needed.

#include "security/timestamp_authority.h"
#include "utils/logger.h"

#include <openssl/ts.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/obj_mac.h>
#include <openssl/rand.h>
#include <curl/curl.h>

#include <cstring>
#include <sstream>
#include <iomanip>

namespace themis { namespace security {

class TimestampAuthority::Impl {
public:
    CURL* curl = nullptr;
    Impl() { curl = curl_easy_init(); }
    ~Impl() { if (curl) curl_easy_cleanup(curl); }
};

static const EVP_MD* selectDigest(const std::string& algo){
    if(algo == "SHA384") return EVP_sha384();
    if(algo == "SHA512") return EVP_sha512();
    return EVP_sha256();
}

static std::string hex(const std::vector<uint8_t>& data){
    static const char* d = "0123456789abcdef"; std::string out; out.reserve(data.size()*2);
    for(auto b: data){ out.push_back(d[(b>>4)&0xF]); out.push_back(d[b&0xF]); }
    return out;
}

static std::string b64Encode(const std::vector<uint8_t>& data){
    BIO* b64 = BIO_new(BIO_f_base64()); BIO* mem = BIO_new(BIO_s_mem());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, mem);
    BIO_write(b64, data.data(), (int)data.size());
    BIO_flush(b64);
    BUF_MEM* ptr; BIO_get_mem_ptr(b64, &ptr);
    std::string out(ptr->data, ptr->length);
    BIO_free_all(b64);
    return out;
}

static std::vector<uint8_t> b64Decode(const std::string& s){
    BIO* b64 = BIO_new(BIO_f_base64()); BIO* mem = BIO_new_mem_buf(s.data(), (int)s.size());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    mem = BIO_push(b64, mem);
    std::vector<uint8_t> out(s.size());
    int len = BIO_read(mem, out.data(), (int)out.size());
    if(len < 0) len = 0; out.resize(len);
    BIO_free_all(mem);
    return out;
}

TimestampAuthority::TimestampAuthority(TSAConfig config)
    : impl_(std::make_unique<Impl>()), config_(std::move(config)) {}
TimestampAuthority::~TimestampAuthority() = default;
TimestampAuthority::TimestampAuthority(TimestampAuthority&&) noexcept = default;
TimestampAuthority& TimestampAuthority::operator=(TimestampAuthority&&) noexcept = default;

std::vector<uint8_t> TimestampAuthority::computeHash(const std::vector<uint8_t>& data){
    const EVP_MD* md = selectDigest(config_.hash_algorithm);
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    std::vector<uint8_t> out(EVP_MD_size(md));
    unsigned int outlen=0;
    EVP_DigestInit_ex(ctx, md, nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    EVP_DigestFinal_ex(ctx, out.data(), &outlen);
    out.resize(outlen);
    EVP_MD_CTX_free(ctx);
    return out;
}

std::vector<uint8_t> TimestampAuthority::generateNonce(size_t bytes){
    std::vector<uint8_t> n(bytes);
    if(RAND_bytes(n.data(), (int)bytes) != 1){
        for(size_t i=0;i<bytes;++i) n[i] = (uint8_t)i; // fallback deterministic
    }
    return n;
}

std::vector<uint8_t> TimestampAuthority::createTSPRequest(const std::vector<uint8_t>& hash,const std::vector<uint8_t>& nonce){
    TS_REQ* req = TS_REQ_new(); if(!req){ last_error_="TS_REQ_new failed"; return {}; }
    TS_REQ_set_version(req,1);
    // Message imprint
    TS_MSG_IMPRINT* imprint = TS_MSG_IMPRINT_new();
    const EVP_MD* md = selectDigest(config_.hash_algorithm);
    X509_ALGOR* algo = X509_ALGOR_new();
    X509_ALGOR_set0(algo, OBJ_nid2obj(EVP_MD_type(md)), V_ASN1_NULL, nullptr);
    TS_MSG_IMPRINT_set_algo(imprint, algo);
    ASN1_OCTET_STRING* hash_asn1 = ASN1_OCTET_STRING_new();
    ASN1_OCTET_STRING_set(hash_asn1, hash.data(), (int)hash.size());
    TS_MSG_IMPRINT_set_msg(imprint, hash_asn1);
    TS_REQ_set_msg_imprint(req, imprint);
    // Nonce
    if(!nonce.empty()){
        ASN1_INTEGER* nonce_i = ASN1_INTEGER_new();
        BIGNUM* bn = BN_bin2bn(nonce.data(), (int)nonce.size(), nullptr);
        BN_to_ASN1_INTEGER(bn, nonce_i); BN_free(bn);
        TS_REQ_set_nonce(req, nonce_i);
    }
    if(config_.cert_req) TS_REQ_set_cert_req(req,1);
    if(!config_.policy_oid.empty()){
        ASN1_OBJECT* policy = OBJ_txt2obj(config_.policy_oid.c_str(),1);
        if(policy){ TS_REQ_set_policy_id(req, policy); ASN1_OBJECT_free(policy); }
    }
    unsigned char* der=nullptr; int len=i2d_TS_REQ(req,&der);
    std::vector<uint8_t> out;
    if(len>0 && der){ out.assign(der, der+len); OPENSSL_free(der); }
    TS_REQ_free(req); // imprint, algo, hash_asn1 freed through req
    return out;
}

size_t curlWrite(char* ptr,size_t size,size_t nmemb,void* userdata){
    auto* vec = reinterpret_cast<std::vector<uint8_t>*>(userdata);
    size_t total = size*nmemb;
    vec->insert(vec->end(), (uint8_t*)ptr, (uint8_t*)ptr + total);
    return total;
}

std::vector<uint8_t> TimestampAuthority::sendTSPRequest(const std::vector<uint8_t>& request){
    if(!impl_->curl){ last_error_="CURL init failed"; return {}; }
    std::vector<uint8_t> response;
    curl_easy_setopt(impl_->curl, CURLOPT_URL, config_.url.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_POST, 1L);
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, request.data());
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDSIZE, request.size());
    curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT, config_.timeout_seconds);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(impl_->curl, CURLOPT_WRITEDATA, &response);
    struct curl_slist* headers=nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/timestamp-query");
    curl_easy_setopt(impl_->curl, CURLOPT_HTTPHEADER, headers);
    CURLcode res = curl_easy_perform(impl_->curl);
    curl_slist_free_all(headers);
    if(res!=CURLE_OK){ last_error_ = std::string("curl error: ")+curl_easy_strerror(res); return {}; }
    long code=0; curl_easy_getinfo(impl_->curl, CURLINFO_RESPONSE_CODE, &code);
    if(code!=200){ last_error_ = "HTTP status "+std::to_string(code); return {}; }
    return response;
}

TimestampToken TimestampAuthority::parseTSPResponse(const std::vector<uint8_t>& respBytes){
    TimestampToken token;
    const unsigned char* p = respBytes.data();
    TS_RESP* resp = d2i_TS_RESP(nullptr, &p, (long)respBytes.size());
    if(!resp){ token.error_message="d2i_TS_RESP failed"; return token; }
    TS_STATUS_INFO* status = TS_RESP_get_status_info(resp);
    ASN1_INTEGER* st_int = TS_STATUS_INFO_get0_status(status);
    token.pki_status = ASN1_INTEGER_get(st_int);
    if(token.pki_status != 0 && token.pki_status != 1){ token.error_message="TSA rejected"; TS_RESP_free(resp); return token; }
    PKCS7* pkcs7 = TS_RESP_get_token(resp);
    if(!pkcs7){ token.error_message="No PKCS7"; TS_RESP_free(resp); return token; }
    unsigned char* der=nullptr; int der_len=i2d_PKCS7(pkcs7,&der);
    if(der_len>0 && der){ token.token_der.assign(der, der+der_len); OPENSSL_free(der); }
    token.token_b64 = b64Encode(token.token_der);
    TS_TST_INFO* tst = PKCS7_to_TS_TST_INFO(pkcs7);
    if(tst){
        ASN1_GENERALIZEDTIME* gen = TS_TST_INFO_get_time(tst);
        if(gen){ std::string g(reinterpret_cast<char*>(gen->data), gen->length); token.timestamp_utc = g; }
        ASN1_INTEGER* serial = TS_TST_INFO_get_serial(tst);
        if(serial){ BIGNUM* bn = ASN1_INTEGER_to_BN(serial,nullptr); char* hexStr = BN_bn2hex(bn); token.serial_number = hexStr; OPENSSL_free(hexStr); BN_free(bn);}        
        ASN1_OBJECT* policy = TS_TST_INFO_get_policy_id(tst);
        if(policy){ char buf[128]; OBJ_obj2txt(buf,sizeof(buf),policy,1); token.policy_oid=buf; }
        TS_TST_INFO_free(tst);
    }
    token.success=true; token.verified=false; // separate verification step
    TS_RESP_free(resp);
    return token;
}

TimestampToken TimestampAuthority::getTimestampForHash(const std::vector<uint8_t>& hash){
    auto nonce = generateNonce();
    auto req = createTSPRequest(hash, nonce);
    if(req.empty()){ TimestampToken t; t.error_message=last_error_; return t; }
    auto resp = sendTSPRequest(req);
    if(resp.empty()){ TimestampToken t; t.error_message=last_error_; return t; }
    auto token = parseTSPResponse(resp);
    token.nonce = nonce;
    token.hash_algorithm = config_.hash_algorithm;
    return token;
}

TimestampToken TimestampAuthority::getTimestamp(const std::vector<uint8_t>& data){
    auto h = computeHash(data);
    return getTimestampForHash(h);
}

bool TimestampAuthority::verifyTimestampForHash(const std::vector<uint8_t>& hash,const TimestampToken& token){
    if(token.token_der.empty()) return false;
    const unsigned char* p = token.token_der.data();
    PKCS7* pkcs7 = d2i_PKCS7(nullptr,&p,(long)token.token_der.size());
    if(!pkcs7) return false;
    TS_TST_INFO* tst = PKCS7_to_TS_TST_INFO(pkcs7);
    if(!tst){ PKCS7_free(pkcs7); return false; }
    TS_MSG_IMPRINT* imprint = TS_TST_INFO_get_msg_imprint(tst);
    ASN1_OCTET_STRING* os = TS_MSG_IMPRINT_get_msg(imprint);
    bool match = (os->length == (int)hash.size() && std::memcmp(os->data, hash.data(), hash.size())==0);
    TS_TST_INFO_free(tst); PKCS7_free(pkcs7);
    return match;
}

bool TimestampAuthority::verifyTimestamp(const std::vector<uint8_t>& data,const TimestampToken& token){
    auto h = computeHash(data);
    return verifyTimestampForHash(h, token);
}

TimestampToken TimestampAuthority::parseToken(const std::vector<uint8_t>& der){ return parseTSPResponse(der); }
TimestampToken TimestampAuthority::parseToken(const std::string& b64){ auto der = b64Decode(b64); return parseTSPResponse(der); }

std::optional<std::string> TimestampAuthority::getTSACertificate(){ return std::nullopt; }

bool TimestampAuthority::isAvailable(){
    if(!impl_->curl) return false;
    curl_easy_setopt(impl_->curl, CURLOPT_URL, config_.url.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(impl_->curl, CURLOPT_TIMEOUT, 5L);
    CURLcode res = curl_easy_perform(impl_->curl);
    return res == CURLE_OK;
}

std::string TimestampAuthority::getLastError() const { return last_error_; }

} } // namespace themis::security

#endif // THEMIS_USE_OPENSSL_TSA
