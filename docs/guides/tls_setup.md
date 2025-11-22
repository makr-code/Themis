# TLS/SSL Setup Guide for ThemisDB

## Overview

ThemisDB supports both HTTP and HTTPS modes with optional mutual TLS (mTLS) for enhanced security. This guide covers certificate generation, configuration, and testing.

## Security Features

### Transport Layer Security (TLS)
- **TLS 1.3 Support**: Modern, secure protocol with perfect forward secrecy
- **TLS 1.2 Fallback**: Configurable for compatibility with legacy clients
- **Strong Cipher Suites**: ECDHE-RSA-AES256-GCM-SHA384, ECDHE-RSA-AES128-GCM-SHA256, ChaCha20-Poly1305
- **Disabled Weak Protocols**: SSLv2, SSLv3, TLSv1.0, TLSv1.1 explicitly disabled

### Mutual TLS (mTLS)
- **Client Certificate Verification**: Enforce client authentication with X.509 certificates
- **Certificate Chain Validation**: Verify certificates against trusted CA bundle
- **Client Identity Logging**: Extract and log client certificate DN for audit trails

### HTTP Security Headers
- **HSTS (Strict-Transport-Security)**: Forces HTTPS for 1 year with subdomain inclusion
- **X-Frame-Options**: Prevents clickjacking attacks (DENY)
- **X-Content-Type-Options**: Prevents MIME type sniffing (nosniff)
- **Content-Security-Policy**: Restricts content sources (default-src 'self')

---

## Certificate Generation

### Using the Test Certificate Script

For **development and testing only**, use the provided script to generate self-signed certificates:

```bash
# Generate certificates in default location (config/certs)
./scripts/generate_test_certs.sh

# Generate certificates in custom location
./scripts/generate_test_certs.sh /path/to/certs
```

This script generates:
- **CA certificate and key** (`ca.crt`, `ca.key`) - Root authority for signing
- **Server certificate and key** (`server.crt`, `server.key`) - For HTTPS endpoint
- **Client certificate and key** (`client.crt`, `client.key`) - For mTLS authentication
- **Full chain files** (`server-fullchain.pem`, `client-bundle.pem`) - Combined PEM formats

### Production Certificates

For **production environments**, obtain certificates from a trusted Certificate Authority (CA):

1. **Internal CA** (e.g., Active Directory Certificate Services, HashiCorp Vault)
2. **Public CA** (e.g., Let's Encrypt, DigiCert, GlobalSign)
3. **Enterprise PKI** (integrate with existing organizational PKI infrastructure)

#### Let's Encrypt Example

```bash
# Install certbot
sudo apt-get install certbot

# Obtain certificate (HTTP-01 challenge)
sudo certbot certonly --standalone -d themisdb.example.com

# Certificates will be in /etc/letsencrypt/live/themisdb.example.com/
# - fullchain.pem (server certificate + intermediate CA)
# - privkey.pem (private key)
```

---

## Configuration

### Environment Variables

ThemisDB TLS configuration is controlled via environment variables:

#### Basic TLS (One-Way Authentication)

```bash
# Enable HTTPS
export THEMIS_TLS_ENABLED=1

# Server certificate (PEM format)
export THEMIS_TLS_CERT=/path/to/server.crt

# Server private key (PEM format)
export THEMIS_TLS_KEY=/path/to/server.key

# Minimum TLS version (TLSv1.2 or TLSv1.3, default: TLSv1.3)
export THEMIS_TLS_MIN_VERSION=TLSv1.3

# Optional: Custom cipher list (OpenSSL format)
# export THEMIS_TLS_CIPHER_LIST="ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256"
```

#### Mutual TLS (Two-Way Authentication)

```bash
# Enable HTTPS with mTLS
export THEMIS_TLS_ENABLED=1
export THEMIS_TLS_CERT=/path/to/server.crt
export THEMIS_TLS_KEY=/path/to/server.key

# CA certificate for client verification
export THEMIS_TLS_CA_CERT=/path/to/ca.crt

# Require client certificate (enforce mTLS)
export THEMIS_TLS_REQUIRE_CLIENT_CERT=1
```

### Code Configuration (C++ API)

```cpp
#include "server/http_server.h"

themis::server::HttpServer::Config config;
config.host = "0.0.0.0";
config.port = 8443; // Standard HTTPS port

// Enable TLS
config.enable_tls = true;
config.tls_cert_path = "/path/to/server.crt";
config.tls_key_path = "/path/to/server.key";
config.tls_min_version = "TLSv1.3";

// Enable mTLS
config.tls_ca_cert_path = "/path/to/ca.crt";
config.tls_require_client_cert = true;

auto server = std::make_unique<themis::server::HttpServer>(
    config, storage, secondary_index, graph_index, vector_index, tx_manager
);
```

---

## Testing

### Test HTTPS Connection (One-Way TLS)

```bash
# Health check with CA verification
curl --cacert config/certs/ca.crt https://localhost:8443/health

# Skip verification (development only)
curl -k https://localhost:8443/health

# Test with verbose output
curl -v --cacert config/certs/ca.crt https://localhost:8443/metrics
```

### Test mTLS Connection (Mutual TLS)

```bash
# Authenticate with client certificate
curl --cacert config/certs/ca.crt \
     --cert config/certs/client.crt \
     --key config/certs/client.key \
     https://localhost:8443/health

# Test authenticated API endpoint
curl --cacert config/certs/ca.crt \
     --cert config/certs/client.crt \
     --key config/certs/client.key \
     -H "Authorization: Bearer YOUR_TOKEN" \
     https://localhost:8443/api/entities/test-key
```

### Verify Certificate Details

```bash
# Inspect server certificate
openssl x509 -in config/certs/server.crt -text -noout

# Check certificate chain
openssl verify -CAfile config/certs/ca.crt config/certs/server.crt

# Test TLS handshake
openssl s_client -connect localhost:8443 -CAfile config/certs/ca.crt

# Test mTLS handshake with client cert
openssl s_client -connect localhost:8443 \
    -CAfile config/certs/ca.crt \
    -cert config/certs/client.crt \
    -key config/certs/client.key
```

---

## Security Best Practices

### Certificate Management

1. **Private Key Protection**: Store private keys with restricted permissions (`chmod 600`)
2. **Certificate Rotation**: Implement automated certificate renewal (e.g., Let's Encrypt auto-renewal)
3. **Certificate Revocation**: Monitor CRL/OCSP for revoked certificates (future enhancement)
4. **Key Separation**: Never reuse private keys across environments (dev/staging/prod)

### Cipher Suite Selection

Default strong ciphers (automatically configured):
- `ECDHE-RSA-AES256-GCM-SHA384` - Perfect Forward Secrecy (PFS) with AES-256
- `ECDHE-RSA-AES128-GCM-SHA256` - PFS with AES-128 (performance/security balance)
- `ECDHE-RSA-CHACHA20-POLY1305` - PFS with ChaCha20 (mobile/IoT devices)

**Avoid weak ciphers** (automatically disabled):
- RC4, MD5, 3DES, DES, EXPORT ciphers
- Anonymous DH (aDH), NULL encryption

### TLS Version Policy

- **Recommended**: TLS 1.3 only (modern clients, highest security)
- **Compatible**: TLS 1.2+ (legacy client support, still secure)
- **Prohibited**: TLS 1.1, TLS 1.0, SSLv3, SSLv2 (deprecated, known vulnerabilities)

### mTLS Use Cases

Enable mutual TLS for:
- **Service-to-Service Communication** (microservices authentication)
- **High-Security APIs** (financial, healthcare, government sectors)
- **Zero-Trust Networks** (verify every client connection)
- **Compliance Requirements** (PCI-DSS, HIPAA, GDPR)

---

## Troubleshooting

### Common Errors

#### 1. `TLS initialization failed: No such file or directory`
**Cause**: Certificate or key file path is incorrect.  
**Solution**: Verify paths with `ls -l $THEMIS_TLS_CERT` and ensure files are readable.

#### 2. `TLS handshake error: certificate verify failed`
**Cause**: Client cannot verify server certificate (CA not trusted).  
**Solution**: Add `--cacert` with CA certificate or install CA in system trust store.

#### 3. `mTLS: no client certificate presented despite requirement`
**Cause**: Client did not provide certificate in mTLS mode.  
**Solution**: Ensure client uses `--cert` and `--key` flags, or disable mTLS requirement.

#### 4. `SSL shutdown error: stream truncated`
**Cause**: Client closed connection abruptly (normal during testing).  
**Solution**: Non-critical, can be ignored in development.

### Debug TLS Issues

```bash
# Enable OpenSSL debug logging
export SSLKEYLOGFILE=/tmp/sslkeys.log
curl --cacert config/certs/ca.crt https://localhost:8443/health

# Analyze TLS handshake with tcpdump
sudo tcpdump -i lo -w /tmp/tls.pcap port 8443
# Open /tmp/tls.pcap in Wireshark with SSLKEYLOGFILE for decryption

# Check server logs for TLS details
tail -f data/logs/themis.log | grep -i "tls\|ssl\|handshake"
```

---

## Future Enhancements

Planned TLS/PKI features:
- [ ] OCSP Stapling (RFC 6066) for certificate revocation checking
- [ ] Certificate Pinning for HSM/TSA outbound connections
- [ ] Automated Let's Encrypt integration with ACME protocol
- [ ] Hardware Security Module (HSM) integration for private key storage
- [ ] Certificate Transparency (CT) log monitoring
- [ ] TLS session resumption (session IDs, session tickets)

---

## References

- [RFC 8446: TLS 1.3](https://tools.ietf.org/html/rfc8446)
- [Mozilla SSL Configuration Generator](https://ssl-config.mozilla.org/)
- [OWASP TLS Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Transport_Layer_Protection_Cheat_Sheet.html)
- [Qualys SSL Labs Server Test](https://www.ssllabs.com/ssltest/)
- [NIST SP 800-52 Rev. 2: TLS Guidelines](https://csrc.nist.gov/publications/detail/sp/800-52/rev-2/final)

---

## Support

For issues or questions:
- **Security Issues**: Report to security@themisdb.io (do not open public issues)
- **General Help**: GitHub Issues or Discussions
- **Documentation**: https://docs.themisdb.io/security/tls
