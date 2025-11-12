#include "security/cms_signing.h"
#include <openssl/cms.h>
#include <openssl/pem.h>
#include <openssl/bio.h>
#include <stdexcept>

namespace themis {

CMSSigningService::CMSSigningService(std::shared_ptr<X509> cert, std::shared_ptr<EVP_PKEY> pkey)
    : cert_(std::move(cert)), pkey_(std::move(pkey)) {}

CMSSigningService::~CMSSigningService() = default;

SigningResult CMSSigningService::sign(const std::vector<uint8_t>& data, const std::string& /*key_id*/) {
    SigningResult res;
    res.algorithm = "CMS/DETACHED+SHA256";

    BIO* in = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    if (!in) throw std::runtime_error("BIO_new_mem_buf failed");

    CMS_ContentInfo* cms = CMS_sign(cert_.get(), pkey_.get(), nullptr, in, CMS_DETACHED | CMS_BINARY);
    BIO_free(in);
    if (!cms) {
        throw std::runtime_error("CMS_sign failed");
    }

    BIO* out = BIO_new(BIO_s_mem());
    if (!out) {
        CMS_ContentInfo_free(cms);
        throw std::runtime_error("BIO_new failed");
    }

    if (i2d_CMS_bio(out, cms) <= 0) {
        BIO_free(out);
        CMS_ContentInfo_free(cms);
        throw std::runtime_error("i2d_CMS_bio failed");
    }

    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(out, &bptr);
    if (bptr && bptr->length > 0) {
        res.signature.assign(reinterpret_cast<uint8_t*>(bptr->data), reinterpret_cast<uint8_t*>(bptr->data) + bptr->length);
    }

    BIO_free(out);
    CMS_ContentInfo_free(cms);
    return res;
}

bool CMSSigningService::verify(const std::vector<uint8_t>& data,
                                const std::vector<uint8_t>& signature,
                                const std::string& /*key_id*/) {
    BIO* sig_bio = BIO_new_mem_buf(signature.data(), static_cast<int>(signature.size()));
    if (!sig_bio) return false;

    CMS_ContentInfo* cms = d2i_CMS_bio(sig_bio, nullptr);
    BIO_free(sig_bio);
    if (!cms) return false;

    BIO* in = BIO_new_mem_buf(data.data(), static_cast<int>(data.size()));
    if (!in) {
        CMS_ContentInfo_free(cms);
        return false;
    }

    X509_STORE* store = X509_STORE_new();
    if (!store) {
        BIO_free(in);
        CMS_ContentInfo_free(cms);
        return false;
    }

    // Add our cert as trusted for verification (self-signed test use-case)
    if (X509_STORE_add_cert(store, cert_.get()) != 1) {
        X509_STORE_free(store);
        BIO_free(in);
        CMS_ContentInfo_free(cms);
        return false;
    }

    int flags = CMS_BINARY;
    int ok = CMS_verify(cms, nullptr, store, in, nullptr, flags);

    X509_STORE_free(store);
    BIO_free(in);
    CMS_ContentInfo_free(cms);

    return ok == 1;
}

} // namespace themis
