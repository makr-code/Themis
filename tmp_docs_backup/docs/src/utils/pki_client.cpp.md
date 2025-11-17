# pki_client.cpp

Path: `src/utils/pki_client.cpp`

Purpose: Client utilities for PKI interactions (fetching certs, validating chains).

Public functions / symbols:
- `static std::string base64_encode(const std::vector<uint8_t>& data) {`
- ``
- `for (unsigned char c : s) {`
- `if (c < 128) {`
- `if (valb >= 0) {`
- `for (size_t i = 0; i < bytes / 8; ++i) {`
- `if (pkey) {`
- `if (rsa) {`
- `if (ok == 1) {`
- `if (pub) {`
- `oss << std::hex << dis(gen);`
- `BIO_free(bio);`
- `BN_free(bn);`
- `X509_free(cert);`
- `RSA_free(rsa);`
- `EVP_PKEY_free(pub);`
- `EVP_PKEY_free(pkey);`

