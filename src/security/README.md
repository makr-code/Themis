# Security Module

Encryption, key management, and PKI integration implementation for ThemisDB.

## Components

- Field-level encryption
- Key cache
- Key providers (Mock, PKI, Vault)
- Encrypted field handling
- HKDF key derivation

## Features

- Column-level encryption
- Multiple key provider backends
- Key rotation support
- HSM integration
- PKI/eIDAS qualified signatures

## Documentation

For security documentation, see:
- [Encrypted Field](../../docs/src/security/encrypted_field.cpp.md)
- [Field Encryption](../../docs/src/security/field_encryption.cpp.md)
- [Key Cache](../../docs/src/security/key_cache.cpp.md)
- [Mock Key Provider](../../docs/src/security/mock_key_provider.cpp.md)
- [PKI Key Provider](../../docs/src/security/pki_key_provider.cpp.md)
- [Vault Key Provider](../../docs/src/security/vault_key_provider.cpp.md)
- [Encryption Strategy](../../docs/encryption_strategy.md)
- [Column Encryption](../../docs/column_encryption.md)
- [Security Documentation](../../docs/security/)
