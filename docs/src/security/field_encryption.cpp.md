# field_encryption.cpp

Path: `src/security/field_encryption.cpp`

Purpose: High level FieldEncryption API. Design references mention `encryptEntityBatch`; verify and document batch API and HKDF integration.

Public functions / symbols:
- ``
- `if (i == 3) {`
- `if (i == 4) {`
- `: key_provider_(key_provider)`
- `if (!key_provider_) {`
- `if (!ctx) {`
- `if (ret <= 0) {`
- `<< base64_encode(tag);`
- `std::stringstream ss(b64);`
- `return encrypt(plaintext_bytes, key_id);`
- `return encryptInternal(plaintext_bytes, key_id, key_version, key);`
- `std::vector<uint8_t> iv(12);  // 96 bits for GCM`
- `throw EncryptionException("Failed to generate random IV");`
- `throw EncryptionException("Failed to create cipher context");`
- `throw EncryptionException("Failed to initialize cipher");`
- `throw EncryptionException("Failed to set IV length");`
- `throw EncryptionException("Failed to set key and IV");`
- `throw EncryptionException("Encryption failed");`
- `throw EncryptionException("Failed to finalize encryption");`
- `throw EncryptionException("Failed to get authentication tag");`
- `EVP_CIPHER_CTX_free(ctx);`
- `throw DecryptionException("IV must be 12 bytes");`
- `throw DecryptionException("Tag must be 16 bytes");`
- `throw DecryptionException("Failed to create cipher context");`
- `throw DecryptionException("Failed to initialize cipher");`
- `throw DecryptionException("Failed to set IV length");`
- `throw DecryptionException("Failed to set key and IV");`
- `throw DecryptionException("Decryption failed");`
- `throw DecryptionException("Failed to set authentication tag");`
- `throw DecryptionException("Authentication failed - data may have been tampered with");`

Notes / TODOs:
- If `encryptEntityBatch` is missing, add implementation plan link to `docs/development/feature_status_changefeed_encryption.md`.
