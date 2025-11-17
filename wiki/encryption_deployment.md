# Themis Column-Level Encryption - Production Deployment Guide

**Version:** 1.0  
**Last Updated:** 30. Oktober 2025  
**Target Audience:** DevOps Engineers, Security Engineers, Database Administrators

---

## Table of Contents

1. [Overview](#overview)
2. [Prerequisites](#prerequisites)
3. [Architecture](#architecture)
4. [HashiCorp Vault Setup](#hashicorp-vault-setup)
5. [Key Management Strategy](#key-management-strategy)
6. [Application Configuration](#application-configuration)
7. [Migration from Plaintext](#migration-from-plaintext)
8. [Key Rotation Procedures](#key-rotation-procedures)
9. [Monitoring & Alerting](#monitoring--alerting)
10. [Disaster Recovery](#disaster-recovery)
11. [Security Best Practices](#security-best-practices)
12. [Troubleshooting](#troubleshooting)
13. [Performance Tuning](#performance-tuning)

---

## Overview

Themis implements **column-level encryption** using **AES-256-GCM** to protect sensitive data at rest. This guide covers deploying the encryption system in production with HashiCorp Vault as the key management backend.

### Key Features

- ✅ **AES-256-GCM** encryption (NIST-approved)
- ✅ **Authenticated encryption** (integrity + confidentiality)
- ✅ **Hardware acceleration** (AES-NI auto-detected)
- ✅ **Key versioning** for zero-downtime rotation
- ✅ **Vault integration** for enterprise key management
- ✅ **Transparent field-level encryption** (minimal code changes)

### Compliance Coverage

| Regulation | Requirement | Themis Implementation |
|------------|-------------|----------------------|
| **GDPR** | Data encryption at rest | ✅ AES-256-GCM |
| **HIPAA** | PHI encryption | ✅ Separate key for medical data |
| **PCI DSS** | Cardholder data protection | ✅ Field-level encryption |
| **SOC 2** | Key management controls | ✅ Vault integration + audit logs |
| **CCPA** | Consumer data protection | ✅ Right to be forgotten support |

---

## Prerequisites

### Infrastructure Requirements

#### HashiCorp Vault
- **Version:** Vault 1.15+ recommended
- **Deployment:** HA cluster (3+ nodes) for production
- **Storage Backend:** Consul (recommended) or Raft integrated storage
- **TLS:** Required for production (mutual TLS recommended)

#### Application Servers
- **CPU:** AES-NI support (Intel/AMD x86-64)
  - Check: `grep -E 'aes|sse4_2' /proc/cpuinfo` (Linux)
  - Check: `sysctl -a | grep machdep.cpu.features` (macOS)
- **Memory:** +512MB heap for key cache
- **Network:** Low-latency connection to Vault (<5ms RTT recommended)

#### Database
- **RocksDB:** Storage for encrypted data
- **Disk:** SSD recommended for encrypted blob performance
- **Space:** Plan for 20-30% overhead vs plaintext

### Software Dependencies

```bash
# Required libraries (installed via vcpkg)
curl >= 8.0
openssl >= 3.0
nlohmann-json >= 3.11
rocksdb >= 8.0
```

### Access Requirements

- **Vault Admin Access:** For initial setup and key creation
- **Application Service Account:** Vault token or AppRole authentication
- **Network Access:** Application → Vault (port 8200, TLS)

---

## Architecture

### Component Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    Application Layer                     │
├─────────────────────────────────────────────────────────┤
│                                                           │
│  ┌─────────────┐     ┌──────────────────┐              │
│  │   User      │────▶│ EncryptedField<T>│              │
│  │   Customer  │     │   - email        │              │
│  │   Document  │     │   - ssn          │              │
│  └─────────────┘     │   - credit_score │              │
│                      └──────────┬───────┘              │
│                                 │                        │
│                      ┌──────────▼────────┐              │
│                      │  FieldEncryption  │              │
│                      │  (AES-256-GCM)    │              │
│                      └──────────┬────────┘              │
│                                 │                        │
│                      ┌──────────▼────────┐              │
│                      │  VaultKeyProvider │              │
│                      │  - Key caching    │              │
│                      │  - Token refresh  │              │
│                      └──────────┬────────┘              │
└──────────────────────────────────┼──────────────────────┘
                                   │ HTTPS/TLS
                      ┌────────────▼─────────────┐
                      │   HashiCorp Vault        │
                      │   KV Secrets Engine v2   │
                      ├──────────────────────────┤
                      │  Keys:                   │
                      │   - user_pii (v1, v2)   │
                      │   - user_sensitive (v1) │
                      │   - customer_financial  │
                      └──────────────────────────┘
                                   │
                      ┌────────────▼─────────────┐
                      │   Vault Storage          │
                      │   (Consul/Raft)          │
                      └──────────────────────────┘
```

### Data Flow

**Write Path (Encryption):**
```
1. User.email = "alice@example.com"
2. EncryptedField.encrypt("alice@example.com", "user_pii")
3. VaultKeyProvider.getKey("user_pii") → [Check cache]
4. If cache miss: HTTP GET /v1/themis/data/keys/user_pii
5. Vault returns: {data: {key: "<base64>", version: 2}}
6. Cache key for 1 hour
7. FieldEncryption.encrypt(plaintext, key) → AES-256-GCM
8. Generate random IV (96 bits)
9. Encrypt + generate auth tag (128 bits)
10. Return: "user_pii:2:IV:ciphertext:tag" (base64)
11. Store in RocksDB as JSON: {"email": "user_pii:2:..."}
```

**Read Path (Decryption):**
```
1. Fetch from RocksDB: {"email": "user_pii:2:IV:ciphertext:tag"}
2. EncryptedField.fromBase64("user_pii:2:...")
3. Parse: key_id="user_pii", version=2, IV, ciphertext, tag
4. VaultKeyProvider.getKey("user_pii", version=2) → [Check cache]
5. If cache miss: HTTP GET /v1/themis/data/keys/user_pii?version=2
6. FieldEncryption.decrypt(ciphertext, key, IV, tag)
7. Verify authentication tag (prevents tampering)
8. Decrypt using AES-256-GCM
9. Return plaintext: "alice@example.com"
```

---

## HashiCorp Vault Setup

### Step 1: Deploy Vault Cluster

#### Production HA Setup (Recommended)

```bash
# Using Docker Compose for quick setup
# For production, use Kubernetes/Nomad or systemd

cat > docker-compose.yml <<EOF
version: '3.8'
services:
  vault1:
    image: hashicorp/vault:1.15
    container_name: vault-1
    ports:
      - "8200:8200"
    environment:
      VAULT_ADDR: 'https://0.0.0.0:8200'
      VAULT_API_ADDR: 'https://vault-1:8200'
    volumes:
      - ./vault/config:/vault/config:ro
      - ./vault/data:/vault/data
      - ./vault/logs:/vault/logs
    cap_add:
      - IPC_LOCK
    command: server
  
  vault2:
    image: hashicorp/vault:1.15
    container_name: vault-2
    ports:
      - "8201:8200"
    environment:
      VAULT_ADDR: 'https://0.0.0.0:8200'
      VAULT_API_ADDR: 'https://vault-2:8200'
    volumes:
      - ./vault/config:/vault/config:ro
      - ./vault/data2:/vault/data
      - ./vault/logs2:/vault/logs
    cap_add:
      - IPC_LOCK
    command: server
  
  vault3:
    image: hashicorp/vault:1.15
    container_name: vault-3
    ports:
      - "8202:8200"
    environment:
      VAULT_ADDR: 'https://0.0.0.0:8200'
      VAULT_API_ADDR: 'https://vault-3:8200'
    volumes:
      - ./vault/config:/vault/config:ro
      - ./vault/data3:/vault/data
      - ./vault/logs3:/vault/logs
    cap_add:
      - IPC_LOCK
    command: server

  consul:
    image: hashicorp/consul:1.16
    container_name: consul
    ports:
      - "8500:8500"
    command: agent -server -ui -bootstrap-expect=1 -client=0.0.0.0
EOF

# Vault configuration
mkdir -p vault/config
cat > vault/config/vault.hcl <<EOF
storage "consul" {
  address = "consul:8500"
  path    = "vault/"
}

listener "tcp" {
  address     = "0.0.0.0:8200"
  tls_cert_file = "/vault/config/tls/vault.crt"
  tls_key_file  = "/vault/config/tls/vault.key"
}

api_addr = "https://vault-1:8200"
cluster_addr = "https://vault-1:8201"
ui = true

# Performance tuning
max_lease_ttl = "87600h"  # 10 years
default_lease_ttl = "87600h"

# Enable Prometheus metrics
telemetry {
  prometheus_retention_time = "24h"
  disable_hostname = true
}
EOF

docker-compose up -d
```

#### Generate TLS Certificates

```bash
# Create CA
openssl req -x509 -newkey rsa:4096 -keyout vault/config/tls/ca-key.pem \
  -out vault/config/tls/ca.pem -days 3650 -nodes \
  -subj "/C=US/ST=CA/L=SF/O=Themis/CN=Vault CA"

# Create Vault certificate
openssl req -newkey rsa:4096 -keyout vault/config/tls/vault.key \
  -out vault/config/tls/vault.csr -nodes \
  -subj "/C=US/ST=CA/L=SF/O=Themis/CN=vault.example.com"

openssl x509 -req -in vault/config/tls/vault.csr \
  -CA vault/config/tls/ca.pem -CAkey vault/config/tls/ca-key.pem \
  -CAcreateserial -out vault/config/tls/vault.crt -days 825 \
  -extensions v3_req -extfile <(cat <<EOF
[v3_req]
subjectAltName = @alt_names
[alt_names]
DNS.1 = vault.example.com
DNS.2 = localhost
IP.1 = 127.0.0.1
EOF
)
```

### Step 2: Initialize Vault

```bash
export VAULT_ADDR='https://vault.example.com:8200'
export VAULT_CACERT='/path/to/ca.pem'

# Initialize (DO THIS ONCE)
vault operator init -key-shares=5 -key-threshold=3 > vault-init.txt

# CRITICAL: Store unseal keys and root token securely!
# Distribute unseal keys to different trusted personnel

# Unseal all 3 nodes (requires 3 of 5 keys)
vault operator unseal <key1>
vault operator unseal <key2>
vault operator unseal <key3>

# Login with root token
vault login <root-token>
```

### Step 3: Enable KV Secrets Engine

```bash
# Enable KV v2 secrets engine
vault secrets enable -version=2 -path=themis kv

# Verify
vault secrets list
# Should show:
# themis/    kv    n/a       n/a     n/a        n/a   28h24m30s   n/a
```

### Step 4: Create Encryption Keys

```bash
# Helper script to generate encryption keys
cat > create-encryption-key.sh <<'EOF'
#!/bin/bash
set -e

KEY_ID=$1
DESCRIPTION=$2

if [ -z "$KEY_ID" ]; then
  echo "Usage: $0 <key_id> [description]"
  exit 1
fi

# Generate 256-bit random key
KEY=$(openssl rand -base64 32)

# Store in Vault
vault kv put themis/keys/$KEY_ID \
  key="$KEY" \
  algorithm="AES-256-GCM" \
  version=1 \
  description="$DESCRIPTION" \
  created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

echo "✅ Created key: $KEY_ID"
vault kv get themis/keys/$KEY_ID
EOF

chmod +x create-encryption-key.sh

# Create keys for different data categories
./create-encryption-key.sh user_pii "General user PII (email, phone, address)"
./create-encryption-key.sh user_sensitive "High-sensitivity user data (SSN, medical records)"
./create-encryption-key.sh customer_financial "Financial data (credit scores, income)"
./create-encryption-key.sh payment_info "Payment card data (PCI DSS)"
```

### Step 5: Create Application Policy

```bash
# Policy for Themis application
cat > themis-policy.hcl <<EOF
# Read access to encryption keys
path "themis/data/keys/*" {
  capabilities = ["read", "list"]
}

# Read key metadata (for rotation monitoring)
path "themis/metadata/keys/*" {
  capabilities = ["read", "list"]
}

# Deny write/delete (keys managed by admins only)
path "themis/data/keys/*" {
  capabilities = ["deny"]
  denied_parameters = {
    "*" = []
  }
}
EOF

vault policy write themis-app themis-policy.hcl

# Verify
vault policy read themis-app
```

### Step 6: Configure AppRole Authentication

```bash
# Enable AppRole auth
vault auth enable approle

# Create role for Themis application
vault write auth/approle/role/themis-app \
  token_ttl=1h \
  token_max_ttl=4h \
  token_policies="themis-app" \
  secret_id_ttl=0 \
  secret_id_num_uses=0

# Get role ID
vault read auth/approle/role/themis-app/role-id
# role_id: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx

# Generate secret ID
vault write -f auth/approle/role/themis-app/secret-id
# secret_id: yyyyyyyy-yyyy-yyyy-yyyy-yyyyyyyyyyyy

# Store role_id and secret_id securely (e.g., Kubernetes secrets)
```

---

## Key Management Strategy

### Key Categorization

Organize keys by **data sensitivity** and **rotation frequency**:

| Key ID | Purpose | Data Examples | Rotation Frequency | Compliance |
|--------|---------|---------------|-------------------|------------|
| `user_pii` | General PII | Email, phone, address | 12 months | GDPR, CCPA |
| `user_sensitive` | High-sensitivity PII | SSN, passport, medical ID | 6 months | HIPAA, GDPR |
| `customer_financial` | Financial data | Credit score, income | 6 months | PCI DSS, SOC 2 |
| `payment_info` | Payment cards | Card number, CVV | 3 months | PCI DSS |
| `healthcare_phi` | Protected health info | Diagnoses, prescriptions | 6 months | HIPAA |

### Key Versioning Scheme

```
Key Format: <key_id>:<version>:<iv>:<ciphertext>:<tag>
Example:    user_pii:2:ghQO6IvYuVdlrXna:qh6kXp9P6dPJlceX4hMes4U=:H9/fjZNKYg==

Version Lifecycle:
  v1: ACTIVE    → Encrypts new data, decrypts old data
  v2: ROTATING  → Dual-write phase (v1 deprecated, v2 active)
  v1: DEPRECATED→ Decrypts old data only (no new encryptions)
  v1: DELETED   → After grace period (90 days), physically deleted
```

### Key Rotation Schedule

```bash
# Automated rotation cron job (run monthly)
cat > /etc/cron.monthly/rotate-encryption-keys.sh <<'EOF'
#!/bin/bash
set -e

VAULT_ADDR="https://vault.example.com:8200"
VAULT_TOKEN="<service-account-token>"

# Rotate keys older than 6 months
for KEY_ID in user_sensitive customer_financial healthcare_phi; do
  CURRENT_VERSION=$(vault kv get -format=json themis/keys/$KEY_ID | jq -r '.data.metadata.version')
  NEW_VERSION=$((CURRENT_VERSION + 1))
  
  NEW_KEY=$(openssl rand -base64 32)
  
  vault kv put themis/keys/$KEY_ID \
    key="$NEW_KEY" \
    algorithm="AES-256-GCM" \
    version=$NEW_VERSION \
    created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    rotated_from_version=$CURRENT_VERSION
  
  echo "✅ Rotated $KEY_ID: v$CURRENT_VERSION → v$NEW_VERSION"
  
  # Trigger application re-encryption job
  curl -X POST https://themis-api.example.com/admin/re-encrypt \
    -H "Authorization: Bearer $ADMIN_TOKEN" \
    -d "{\"key_id\": \"$KEY_ID\", \"target_version\": $NEW_VERSION}"
done
EOF

chmod +x /etc/cron.monthly/rotate-encryption-keys.sh
```

---

## Application Configuration

### VaultKeyProvider Configuration

```cpp
// config/encryption.hpp
#include "security/vault_key_provider.h"

themis::VaultKeyProvider::Config getVaultConfig() {
    themis::VaultKeyProvider::Config config;
    
    // Vault connection
    config.vault_addr = std::getenv("VAULT_ADDR") ?: "https://vault.example.com:8200";
    config.vault_token = std::getenv("VAULT_TOKEN") ?: "";  // From AppRole login
    config.kv_mount_path = "themis";
    config.kv_version = "v2";
    
    // TLS configuration
    config.verify_ssl = true;
    config.ca_cert_path = "/etc/ssl/certs/vault-ca.pem";
    
    // Performance tuning
    config.cache_ttl_seconds = 3600;      // 1 hour cache
    config.cache_capacity = 1000;         // Max 1000 cached keys
    config.request_timeout_ms = 5000;     // 5 second timeout
    
    // Connection pooling (if using custom HTTP client)
    config.max_connections = 10;
    config.keepalive = true;
    
    return config;
}

// Initialize in application startup
void initializeEncryption() {
    auto vault_config = getVaultConfig();
    auto key_provider = std::make_shared<themis::VaultKeyProvider>(vault_config);
    auto encryption = std::make_shared<themis::FieldEncryption>(key_provider);
    
    // Set global encryption for all field types
    themis::EncryptedField<std::string>::setFieldEncryption(encryption);
    themis::EncryptedField<int64_t>::setFieldEncryption(encryption);
    themis::EncryptedField<double>::setFieldEncryption(encryption);
    
    // Warm up cache with frequently used keys
    key_provider->getKey("user_pii");
    key_provider->getKey("user_sensitive");
    key_provider->getKey("customer_financial");
}
```

### Environment Variables

```bash
# Production environment (.env file)
VAULT_ADDR=https://vault.example.com:8200
VAULT_TOKEN=<from-approle-login>
VAULT_CACERT=/etc/ssl/certs/vault-ca.pem
VAULT_NAMESPACE=themis  # For Vault Enterprise

# Optional: Override defaults
ENCRYPTION_CACHE_TTL=3600
ENCRYPTION_CACHE_SIZE=1000
ENCRYPTION_KEY_MOUNT=themis
```

### Kubernetes Deployment

```yaml
# kubernetes/deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: themis-api
  namespace: production
spec:
  replicas: 3
  selector:
    matchLabels:
      app: themis-api
  template:
    metadata:
      labels:
        app: themis-api
    spec:
      serviceAccountName: themis-app
      containers:
      - name: themis-api
        image: themis:latest
        env:
        - name: VAULT_ADDR
          value: "https://vault.vault.svc.cluster.local:8200"
        - name: VAULT_TOKEN
          valueFrom:
            secretKeyRef:
              name: vault-token
              key: token
        - name: VAULT_CACERT
          value: "/vault/tls/ca.crt"
        volumeMounts:
        - name: vault-tls
          mountPath: /vault/tls
          readOnly: true
        resources:
          requests:
            memory: "2Gi"
            cpu: "1000m"
          limits:
            memory: "4Gi"
            cpu: "2000m"
        livenessProbe:
          httpGet:
            path: /health
            port: 8080
          initialDelaySeconds: 30
          periodSeconds: 10
        readinessProbe:
          httpGet:
            path: /ready
            port: 8080
          initialDelaySeconds: 10
          periodSeconds: 5
      volumes:
      - name: vault-tls
        secret:
          secretName: vault-ca-cert

---
# Vault token secret (from AppRole login)
apiVersion: v1
kind: Secret
metadata:
  name: vault-token
  namespace: production
type: Opaque
data:
  token: <base64-encoded-vault-token>
```

---

## Migration from Plaintext

### Phase 1: Assessment (Week 1)

```sql
-- Identify columns to encrypt
SELECT 
  table_name,
  column_name,
  data_type,
  COUNT(*) as row_count,
  SUM(LENGTH(column_name)) as total_bytes
FROM information_schema.columns
WHERE column_name IN ('email', 'ssn', 'phone', 'credit_card')
GROUP BY table_name, column_name;

-- Estimate migration time
-- Rule of thumb: 10,000 rows/second on modern hardware
```

### Phase 2: Schema Changes (Week 2)

```cpp
// Add encrypted columns alongside plaintext (dual-write phase)
struct User {
    std::string id;
    std::string username;
    
    // OLD: Plaintext (deprecated)
    std::string email_plaintext;
    std::string ssn_plaintext;
    
    // NEW: Encrypted
    EncryptedField<std::string> email;
    EncryptedField<std::string> ssn;
    
    // Migration flag
    bool is_encrypted = false;
};
```

### Phase 3: Dual-Write Migration (Week 3-4)

```cpp
// Write to both plaintext and encrypted columns
void saveUser(const User& user) {
    // Write plaintext (for backward compatibility)
    db->put("user:" + user.id + ":email_plain", user.email_plaintext);
    
    // Write encrypted
    user.email.encrypt(user.email_plaintext, "user_pii");
    auto encrypted_blob = user.email.toBase64();
    db->put("user:" + user.id + ":email_enc", encrypted_blob);
    
    // Mark as encrypted
    db->put("user:" + user.id + ":encrypted", "true");
}

// Background migration job
void migrateUserData() {
    auto all_users = db->scan("user:");
    
    for (const auto& [key, value] : all_users) {
        std::string user_id = extractUserId(key);
        
        // Skip if already encrypted
        auto encrypted_flag = db->get("user:" + user_id + ":encrypted");
        if (encrypted_flag == "true") continue;
        
        // Migrate plaintext to encrypted
        auto email_plain = db->get("user:" + user_id + ":email_plain");
        if (!email_plain.empty()) {
            EncryptedField<std::string> email_enc;
            email_enc.encrypt(email_plain, "user_pii");
            db->put("user:" + user_id + ":email_enc", email_enc.toBase64());
        }
        
        db->put("user:" + user_id + ":encrypted", "true");
        
        // Log progress
        std::cout << "Migrated user: " << user_id << std::endl;
    }
}
```

### Phase 4: Switch Reads (Week 5)

```cpp
// Preferentially read from encrypted columns
std::string getUserEmail(const std::string& user_id) {
    // Try encrypted first
    auto encrypted_data = db->get("user:" + user_id + ":email_enc");
    if (!encrypted_data.empty()) {
        auto email_field = EncryptedField<std::string>::fromBase64(encrypted_data);
        return email_field.decrypt();
    }
    
    // Fallback to plaintext (for unmigrated users)
    return db->get("user:" + user_id + ":email_plain");
}
```

### Phase 5: Cleanup (Week 6+)

```cpp
// After 100% migration confirmed, delete plaintext columns
void cleanupPlaintextData() {
    auto all_users = db->scan("user:");
    
    for (const auto& [key, value] : all_users) {
        if (key.find(":email_plain") != std::string::npos ||
            key.find(":ssn_plain") != std::string::npos) {
            
            std::string user_id = extractUserId(key);
            
            // Verify encrypted version exists
            auto encrypted_flag = db->get("user:" + user_id + ":encrypted");
            if (encrypted_flag == "true") {
                // Safe to delete plaintext
                db->del(key);
                std::cout << "Deleted plaintext: " << key << std::endl;
            }
        }
    }
}
```

---

## Key Rotation Procedures

### Manual Rotation Process

```bash
#!/bin/bash
# rotate-key.sh - Manual key rotation script

set -e

KEY_ID=$1
if [ -z "$KEY_ID" ]; then
  echo "Usage: $0 <key_id>"
  exit 1
fi

echo "🔄 Starting key rotation for: $KEY_ID"

# Step 1: Get current version
CURRENT=$(vault kv get -format=json themis/keys/$KEY_ID | jq -r '.data.metadata.version')
NEW_VERSION=$((CURRENT + 1))

echo "📊 Current version: $CURRENT"
echo "📊 New version: $NEW_VERSION"

# Step 2: Generate new key
NEW_KEY=$(openssl rand -base64 32)

# Step 3: Store new version in Vault
vault kv put themis/keys/$KEY_ID \
  key="$NEW_KEY" \
  algorithm="AES-256-GCM" \
  version=$NEW_VERSION \
  created_at="$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
  rotated_from=$CURRENT \
  rotation_reason="Scheduled rotation"

echo "✅ New key version created in Vault"

# Step 4: Trigger application cache invalidation
curl -X POST https://themis-api.example.com/admin/cache/invalidate \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  -d "{\"key_id\": \"$KEY_ID\"}"

echo "✅ Application caches invalidated"

# Step 5: Start background re-encryption
curl -X POST https://themis-api.example.com/admin/re-encrypt \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  -d "{
    \"key_id\": \"$KEY_ID\",
    \"source_version\": $CURRENT,
    \"target_version\": $NEW_VERSION,
    \"batch_size\": 1000
  }"

echo "✅ Re-encryption job started"
echo "🎉 Key rotation completed!"
```

### Re-Encryption Job Implementation

```cpp
// Background job to re-encrypt data with new key version
class ReEncryptionJob {
public:
    struct Config {
        std::string key_id;
        uint32_t source_version;
        uint32_t target_version;
        size_t batch_size = 1000;
        size_t parallelism = 4;
    };
    
    ReEncryptionJob(
        std::shared_ptr<RocksDBWrapper> db,
        std::shared_ptr<FieldEncryption> encryption,
        const Config& config
    ) : db_(db), encryption_(encryption), config_(config) {}
    
    void run() {
        std::cout << "🔄 Starting re-encryption: " << config_.key_id 
                  << " v" << config_.source_version 
                  << " → v" << config_.target_version << std::endl;
        
        auto start = std::chrono::steady_clock::now();
        size_t total_count = 0;
        size_t success_count = 0;
        
        // Scan all encrypted fields
        auto it = db_->newIterator();
        for (it->SeekToFirst(); it->Valid(); it->Next()) {
            std::string key = it->key().ToString();
            std::string value = it->value().ToString();
            
            try {
                // Parse encrypted blob
                json j = json::parse(value);
                
                for (auto& [field_name, field_value] : j.items()) {
                    if (!field_value.is_string()) continue;
                    
                    std::string blob_str = field_value.get<std::string>();
                    
                    // Check if this field uses the key being rotated
                    if (blob_str.find(config_.key_id + ":") == 0) {
                        auto blob = EncryptedBlob::fromBase64(blob_str);
                        
                        if (blob.key_version == config_.source_version) {
                            // Decrypt with old key
                            std::string plaintext = encryption_->decryptToString(blob);
                            
                            // Re-encrypt with new key
                            EncryptedField<std::string> new_field;
                            new_field.encrypt(plaintext, config_.key_id);
                            
                            // Update JSON
                            j[field_name] = new_field.toBase64();
                            
                            success_count++;
                        }
                    }
                }
                
                // Write updated record
                db_->put(key, j.dump());
                total_count++;
                
                if (total_count % 1000 == 0) {
                    std::cout << "Progress: " << total_count << " records processed, "
                              << success_count << " fields re-encrypted" << std::endl;
                }
                
            } catch (const std::exception& e) {
                std::cerr << "Error processing key " << key << ": " << e.what() << std::endl;
            }
        }
        
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        
        std::cout << "✅ Re-encryption completed:" << std::endl;
        std::cout << "   Records processed: " << total_count << std::endl;
        std::cout << "   Fields re-encrypted: " << success_count << std::endl;
        std::cout << "   Duration: " << duration << "s" << std::endl;
        std::cout << "   Throughput: " << (total_count / duration) << " records/sec" << std::endl;
    }
    
private:
    std::shared_ptr<RocksDBWrapper> db_;
    std::shared_ptr<FieldEncryption> encryption_;
    Config config_;
};
```

---

## Monitoring & Alerting

### Key Metrics

#### Application Metrics

```cpp
// Prometheus metrics (pseudocode)
class EncryptionMetrics {
public:
    // Counters
    prometheus::Counter encryption_operations;
    prometheus::Counter decryption_operations;
    prometheus::Counter encryption_errors;
    prometheus::Counter decryption_errors;
    
    // Histograms
    prometheus::Histogram encryption_duration_ms;
    prometheus::Histogram decryption_duration_ms;
    prometheus::Histogram vault_request_duration_ms;
    
    // Gauges
    prometheus::Gauge key_cache_size;
    prometheus::Gauge key_cache_hit_rate;
    prometheus::Gauge active_key_versions;
};

// Record metrics
void FieldEncryption::encrypt(const std::string& plaintext, const std::string& key_id) {
    auto start = std::chrono::steady_clock::now();
    
    try {
        // ... encryption logic ...
        
        metrics_.encryption_operations.Inc();
        
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start
        ).count();
        metrics_.encryption_duration_ms.Observe(duration);
        
    } catch (const std::exception& e) {
        metrics_.encryption_errors.Inc();
        throw;
    }
}
```

#### Grafana Dashboard

```json
{
  "dashboard": {
    "title": "Themis Encryption Metrics",
    "panels": [
      {
        "title": "Encryption Operations/sec",
        "targets": [
          {
            "expr": "rate(themis_encryption_operations_total[5m])"
          }
        ]
      },
      {
        "title": "Decryption Latency (p95)",
        "targets": [
          {
            "expr": "histogram_quantile(0.95, themis_decryption_duration_ms)"
          }
        ]
      },
      {
        "title": "Cache Hit Rate",
        "targets": [
          {
            "expr": "themis_key_cache_hit_rate"
          }
        ]
      },
      {
        "title": "Vault Request Errors",
        "targets": [
          {
            "expr": "rate(themis_vault_request_errors_total[5m])"
          }
        ]
      }
    ]
  }
}
```

### Alerting Rules

```yaml
# prometheus/alerts.yml
groups:
- name: encryption
  rules:
  
  # Alert if encryption failure rate > 1%
  - alert: HighEncryptionErrorRate
    expr: |
      rate(themis_encryption_errors_total[5m]) / 
      rate(themis_encryption_operations_total[5m]) > 0.01
    for: 5m
    labels:
      severity: critical
    annotations:
      summary: "High encryption error rate detected"
      description: "Encryption error rate is {{ $value | humanizePercentage }}"
  
  # Alert if cache hit rate drops below 80%
  - alert: LowCacheHitRate
    expr: themis_key_cache_hit_rate < 0.8
    for: 10m
    labels:
      severity: warning
    annotations:
      summary: "Low key cache hit rate"
      description: "Cache hit rate is {{ $value | humanizePercentage }}"
  
  # Alert if Vault requests are slow
  - alert: SlowVaultRequests
    expr: |
      histogram_quantile(0.95, 
        rate(themis_vault_request_duration_ms_bucket[5m])
      ) > 100
    for: 5m
    labels:
      severity: warning
    annotations:
      summary: "Slow Vault API requests"
      description: "P95 latency is {{ $value }}ms"
  
  # Alert if a key rotation is overdue
  - alert: KeyRotationOverdue
    expr: |
      (time() - themis_key_last_rotation_timestamp) / 86400 > 180
    for: 1d
    labels:
      severity: warning
    annotations:
      summary: "Key rotation overdue"
      description: "Key {{ $labels.key_id }} has not been rotated in {{ $value }} days"
```

---

## Disaster Recovery

### Backup Procedures

```bash
#!/bin/bash
# backup-vault.sh - Automated Vault backup

BACKUP_DIR="/backups/vault/$(date +%Y%m%d)"
mkdir -p $BACKUP_DIR

# Take Consul snapshot (Vault storage backend)
consul snapshot save $BACKUP_DIR/consul-snapshot.snap

# Export all encryption keys (encrypted with GPG)
vault kv get -format=json themis/keys | \
  gpg --encrypt --recipient backup@example.com > \
  $BACKUP_DIR/encryption-keys.json.gpg

# Backup Vault unseal keys (should be in separate secure location)
# These should already be distributed to key custodians

# Upload to S3 with server-side encryption
aws s3 cp $BACKUP_DIR s3://vault-backups/$(date +%Y%m%d)/ \
  --recursive \
  --sse AES256

echo "✅ Vault backup completed: $BACKUP_DIR"
```

### Recovery Procedures

#### Scenario 1: Lost Vault Token

```bash
# Generate new token from AppRole
ROLE_ID="<stored-role-id>"
SECRET_ID=$(vault write -f auth/approle/role/themis-app/secret-id | \
  grep 'secret_id ' | awk '{print $2}')

NEW_TOKEN=$(vault write auth/approle/login \
  role_id=$ROLE_ID \
  secret_id=$SECRET_ID | \
  grep 'token ' | awk '{print $2}')

# Update application configuration
kubectl set env deployment/themis-api VAULT_TOKEN=$NEW_TOKEN
```

#### Scenario 2: Vault Cluster Failure

```bash
# Restore from backup
consul snapshot restore /backups/vault/20251030/consul-snapshot.snap

# Unseal all Vault nodes
for NODE in vault-1 vault-2 vault-3; do
  vault operator unseal -address=https://$NODE:8200 <unseal-key-1>
  vault operator unseal -address=https://$NODE:8200 <unseal-key-2>
  vault operator unseal -address=https://$NODE:8200 <unseal-key-3>
done

# Verify key recovery
vault kv get themis/keys/user_pii
```

#### Scenario 3: Corrupted Encryption Key

```bash
# If a key becomes corrupted, restore from backup
gpg --decrypt /backups/vault/20251030/encryption-keys.json.gpg | \
  jq '.data.keys.user_pii' | \
  vault kv put themis/keys/user_pii -

# Verify
vault kv get themis/keys/user_pii
```

---

## Security Best Practices

### 1. Key Storage

✅ **DO:**
- Store keys in HashiCorp Vault with encryption at rest
- Use hardware security modules (HSM) for Vault master key
- Implement key versioning for rotation
- Distribute unseal keys to 3+ trusted individuals
- Audit all key access via Vault audit logs

❌ **DON'T:**
- Store keys in environment variables or config files
- Commit keys to version control
- Share keys via email/Slack
- Use single-version keys (prevents rotation)

### 2. Access Control

```hcl
# Principle of least privilege
path "themis/data/keys/user_pii" {
  capabilities = ["read"]
  
  # Allow only from specific IP range
  allowed_parameters = {
    "cidr_list" = ["10.0.0.0/8"]
  }
}

# Separate admin policy for key rotation
path "themis/data/keys/*" {
  capabilities = ["create", "update", "delete"]
  
  # Require MFA for destructive operations
  mfa_methods = ["totp"]
}
```

### 3. Network Security

- **TLS 1.3** for all Vault communications
- **Mutual TLS** (mTLS) in production
- **Network policies** to restrict Vault access
- **Private subnets** for Vault cluster

### 4. Audit Logging

```hcl
# vault/config/audit.hcl
audit {
  type = "file"
  
  options = {
    file_path = "/vault/logs/audit.log"
    log_raw = false  # Don't log sensitive data
    hmac_accessor = true
    mode = "0600"
    format = "json"
  }
}
```

### 5. Monitoring

- **Alert on failed authentication** attempts
- **Monitor key access patterns** for anomalies
- **Track cache hit rates** (should be >80%)
- **Set up dead man's switch** for unseal keys

---

## Troubleshooting

### Issue: Slow Encryption Performance

**Symptoms:**
- Encryption operations >10ms
- High CPU usage
- Low throughput

**Diagnosis:**
```bash
# Check if AES-NI is enabled
lscpu | grep aes
# Should show "aes" in flags

# Check OpenSSL version
openssl version
# Should be 3.0+

# Profile encryption calls
perf record -g ./themis_demo_encryption
perf report
```

**Solutions:**
1. Verify AES-NI hardware support
2. Update OpenSSL to latest version
3. Increase key cache size
4. Use connection pooling for Vault requests

---

### Issue: Vault Connection Timeouts

**Symptoms:**
- `CURL error: Timeout was reached`
- Intermittent decryption failures

**Diagnosis:**
```bash
# Test Vault connectivity
time curl -k https://vault.example.com:8200/v1/sys/health

# Check network latency
ping -c 10 vault.example.com

# Review Vault server logs
vault audit log | grep themis
```

**Solutions:**
1. Increase `request_timeout_ms` in config
2. Deploy Vault closer to application (same datacenter)
3. Enable HTTP/2 keep-alive
4. Scale Vault cluster horizontally

---

### Issue: Cache Thrashing

**Symptoms:**
- Cache hit rate <50%
- Frequent Vault API calls
- Increased latency

**Diagnosis:**
```cpp
// Enable debug logging
auto stats = vault_provider->getCacheStats();
std::cout << "Hit rate: " << stats.hit_rate << std::endl;
std::cout << "Total requests: " << stats.total_requests << std::endl;
std::cout << "Cache size: " << stats.cache_size << std::endl;
```

**Solutions:**
1. Increase `cache_capacity` (default: 1000)
2. Increase `cache_ttl_seconds` (default: 3600)
3. Pre-warm cache on application startup
4. Review key access patterns (consolidate similar keys)

---

## Performance Tuning

### Benchmark Results

**Hardware:** Intel Xeon 8375C (AES-NI), 16GB RAM, NVMe SSD

| Operation | Throughput | Latency (p50) | Latency (p95) |
|-----------|------------|---------------|---------------|
| Encrypt (cached key) | 256,000 ops/sec | 0.004 ms | 0.008 ms |
| Decrypt (cached key) | 200,000 ops/sec | 0.005 ms | 0.010 ms |
| Vault key fetch (cold) | 20 ops/sec | 50 ms | 100 ms |
| DB write (encrypted) | 1,300 ops/sec | 0.75 ms | 2 ms |

### Optimization Checklist

- [x] **Enable AES-NI** hardware acceleration
- [x] **Cache keys** in memory (1h TTL)
- [x] **Use connection pooling** for Vault
- [x] **Batch operations** where possible
- [x] **Pre-warm cache** on startup
- [ ] **Implement circuit breaker** for Vault failures
- [ ] **Use Vault agent** for local caching
- [ ] **Deploy Vault replicas** in each datacenter

---

## Appendix

### A. Key Rotation Checklist

```
□ Generate new key version in Vault
□ Invalidate application key caches
□ Start background re-encryption job
□ Monitor re-encryption progress
□ Verify 100% migration to new version
□ Mark old key version as DEPRECATED
□ Wait 90-day grace period
□ Delete old key version from Vault
□ Update audit logs
```

### B. Emergency Contacts

| Role | Name | Contact | Responsibility |
|------|------|---------|---------------|
| Security Lead | Alice Johnson | alice@example.com | Key management approval |
| DevOps Lead | Bob Smith | bob@example.com | Vault infrastructure |
| On-Call Engineer | <rotation> | oncall@example.com | 24/7 incident response |

### C. Compliance Matrix

| Requirement | Implementation | Evidence |
|-------------|----------------|----------|
| GDPR Art. 32 | AES-256-GCM encryption | Vault audit logs |
| HIPAA §164.312(a)(2)(iv) | Key versioning + rotation | Rotation schedule |
| PCI DSS 3.4 | Cryptographic key management | Vault policies |
| SOC 2 CC6.1 | Access controls | Vault AppRole logs |

---

**Document Version:** 1.0  
**Last Review:** 30. Oktober 2025  
**Next Review:** 30. Januar 2026  
**Owner:** Security Engineering Team
