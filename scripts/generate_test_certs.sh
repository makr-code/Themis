#!/usr/bin/env bash
# THEMIS Test Certificate Generation Script
# Generates self-signed certificates for development and testing

set -euo pipefail

CERTS_DIR="${1:-./config/certs}"
DAYS_VALID=365

echo "=== THEMIS Test Certificate Generator ==="
echo "Certificates will be stored in: $CERTS_DIR"
echo "Validity period: $DAYS_VALID days"

# Create certs directory
mkdir -p "$CERTS_DIR"
cd "$CERTS_DIR"

# ------------------------------------------------------------------------
# 1. Generate CA (Certificate Authority)
# ------------------------------------------------------------------------
echo ""
echo "[1/4] Generating CA certificate and key..."

if [ ! -f ca.key ]; then
    openssl genrsa -out ca.key 4096
    echo "    ✓ CA private key generated (ca.key)"
else
    echo "    ⚠ CA key already exists (ca.key), skipping"
fi

if [ ! -f ca.crt ]; then
    openssl req -x509 -new -nodes -key ca.key -sha256 -days "$DAYS_VALID" -out ca.crt \
        -subj "/C=DE/ST=Bayern/L=Munich/O=ThemisDB/OU=Security/CN=ThemisDB Test CA"
    echo "    ✓ CA certificate generated (ca.crt)"
else
    echo "    ⚠ CA certificate already exists (ca.crt), skipping"
fi

# ------------------------------------------------------------------------
# 2. Generate Server Certificate
# ------------------------------------------------------------------------
echo ""
echo "[2/4] Generating server certificate and key..."

if [ ! -f server.key ]; then
    openssl genrsa -out server.key 4096
    echo "    ✓ Server private key generated (server.key)"
else
    echo "    ⚠ Server key already exists (server.key), skipping"
fi

if [ ! -f server.csr ]; then
    # Create server certificate signing request (CSR)
    openssl req -new -key server.key -out server.csr \
        -subj "/C=DE/ST=Bayern/L=Munich/O=ThemisDB/OU=Server/CN=localhost"
    echo "    ✓ Server CSR generated (server.csr)"
else
    echo "    ⚠ Server CSR already exists (server.csr), skipping"
fi

if [ ! -f server.crt ]; then
    # Create server certificate extensions file for SAN (Subject Alternative Name)
    cat > server.ext <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, nonRepudiation, keyEncipherment, dataEncipherment
subjectAltName = @alt_names

[alt_names]
DNS.1 = localhost
DNS.2 = themisdb
DNS.3 = themisdb.local
IP.1 = 127.0.0.1
IP.2 = ::1
EOF

    # Sign server certificate with CA
    openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
        -out server.crt -days "$DAYS_VALID" -sha256 -extfile server.ext
    echo "    ✓ Server certificate signed by CA (server.crt)"
    rm -f server.ext server.csr
else
    echo "    ⚠ Server certificate already exists (server.crt), skipping"
fi

# ------------------------------------------------------------------------
# 3. Generate Client Certificate (for mTLS)
# ------------------------------------------------------------------------
echo ""
echo "[3/4] Generating client certificate and key..."

if [ ! -f client.key ]; then
    openssl genrsa -out client.key 4096
    echo "    ✓ Client private key generated (client.key)"
else
    echo "    ⚠ Client key already exists (client.key), skipping"
fi

if [ ! -f client.csr ]; then
    openssl req -new -key client.key -out client.csr \
        -subj "/C=DE/ST=Bayern/L=Munich/O=ThemisDB/OU=Client/CN=test-client"
    echo "    ✓ Client CSR generated (client.csr)"
else
    echo "    ⚠ Client CSR already exists (client.csr), skipping"
fi

if [ ! -f client.crt ]; then
    # Create client certificate extensions
    cat > client.ext <<EOF
authorityKeyIdentifier=keyid,issuer
basicConstraints=CA:FALSE
keyUsage = digitalSignature, keyEncipherment
extendedKeyUsage = clientAuth
EOF

    # Sign client certificate with CA
    openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
        -out client.crt -days "$DAYS_VALID" -sha256 -extfile client.ext
    echo "    ✓ Client certificate signed by CA (client.crt)"
    rm -f client.ext client.csr
else
    echo "    ⚠ Client certificate already exists (client.crt), skipping"
fi

# ------------------------------------------------------------------------
# 4. Create combined PEM files for convenience
# ------------------------------------------------------------------------
echo ""
echo "[4/4] Creating combined PEM files..."

if [ ! -f server-fullchain.pem ]; then
    cat server.crt ca.crt > server-fullchain.pem
    echo "    ✓ Server full chain created (server-fullchain.pem)"
else
    echo "    ⚠ Server full chain already exists (server-fullchain.pem), skipping"
fi

if [ ! -f client-bundle.pem ]; then
    cat client.crt client.key > client-bundle.pem
    echo "    ✓ Client bundle created (client-bundle.pem)"
else
    echo "    ⚠ Client bundle already exists (client-bundle.pem), skipping"
fi

# ------------------------------------------------------------------------
# Summary & Instructions
# ------------------------------------------------------------------------
echo ""
echo "=== Certificate Generation Complete ==="
echo ""
echo "Generated files:"
echo "  CA Certificate:          $CERTS_DIR/ca.crt"
echo "  CA Private Key:          $CERTS_DIR/ca.key"
echo "  Server Certificate:      $CERTS_DIR/server.crt"
echo "  Server Private Key:      $CERTS_DIR/server.key"
echo "  Server Full Chain:       $CERTS_DIR/server-fullchain.pem"
echo "  Client Certificate:      $CERTS_DIR/client.crt"
echo "  Client Private Key:      $CERTS_DIR/client.key"
echo "  Client Bundle:           $CERTS_DIR/client-bundle.pem"
echo ""
echo "Example configuration (environment variables):"
echo ""
echo "  # Enable TLS (one-way authentication)"
echo "  export THEMIS_TLS_ENABLED=1"
echo "  export THEMIS_TLS_CERT=$CERTS_DIR/server.crt"
echo "  export THEMIS_TLS_KEY=$CERTS_DIR/server.key"
echo ""
echo "  # Enable mTLS (mutual authentication)"
echo "  export THEMIS_TLS_ENABLED=1"
echo "  export THEMIS_TLS_CERT=$CERTS_DIR/server.crt"
echo "  export THEMIS_TLS_KEY=$CERTS_DIR/server.key"
echo "  export THEMIS_TLS_CA_CERT=$CERTS_DIR/ca.crt"
echo "  export THEMIS_TLS_REQUIRE_CLIENT_CERT=1"
echo ""
echo "  # Test TLS connection with curl"
echo "  curl --cacert $CERTS_DIR/ca.crt https://localhost:8080/health"
echo ""
echo "  # Test mTLS connection with curl"
echo "  curl --cacert $CERTS_DIR/ca.crt \\"
echo "       --cert $CERTS_DIR/client.crt \\"
echo "       --key $CERTS_DIR/client.key \\"
echo "       https://localhost:8080/health"
echo ""
echo "⚠ WARNING: These certificates are for DEVELOPMENT/TESTING ONLY!"
echo "           Do NOT use them in production environments."
echo ""
