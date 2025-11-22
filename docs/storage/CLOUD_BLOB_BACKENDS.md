# Cloud Blob Storage Backends: S3 & Azure

## Overview

ThemisDB unterstützt Cloud-Blob-Storage für große Binärdaten via Plugin-Architektur.

## Backends

### 1. AWS S3 Backend

**Features:**
- Server-side encryption (AES256)
- Automatic retry with exponential backoff
- Multipart upload support (für große Blobs)
- S3 Standard, Intelligent-Tiering, Glacier support
- Cross-region replication

**Dependencies:**
- aws-sdk-cpp >= 1.11.0
- OpenSSL >= 1.1.1

**Configuration:**

```cpp
BlobStorageConfig config;
config.enable_s3 = true;
config.s3_bucket = "themisdb-blobs";
config.s3_region = "eu-central-1";
config.s3_prefix = "production/";

// AWS Credentials via:
// 1. Environment: AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY
// 2. ~/.aws/credentials
// 3. IAM Role (EC2/ECS)
```

**Plugin Manifest:**
```json
{
  "name": "s3_blob_storage",
  "type": "blob_storage",
  "binary": {
    "linux": "themis_blob_s3.so"
  },
  "configuration": {
    "bucket": "themisdb-blobs",
    "region": "eu-central-1"
  }
}
```

**Usage:**
```cpp
// Via PluginManager
auto& pm = PluginManager::instance();
auto* plugin = pm.loadPlugin("s3_blob_storage");
auto* backend = static_cast<IBlobStorageBackend*>(plugin->getInstance());

// Store blob
std::vector<uint8_t> data = {...};
auto ref = backend->put("blob_123", data);
// ref.uri = "s3://themisdb-blobs/production/blob_123.blob"

// Retrieve blob
auto retrieved = backend->get(ref);
```

**S3 Bucket Policy:**
```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Sid": "ThemisDBAccess",
      "Effect": "Allow",
      "Principal": {
        "AWS": "arn:aws:iam::123456789012:role/ThemisDBRole"
      },
      "Action": [
        "s3:PutObject",
        "s3:GetObject",
        "s3:DeleteObject",
        "s3:ListBucket"
      ],
      "Resource": [
        "arn:aws:s3:::themisdb-blobs/*",
        "arn:aws:s3:::themisdb-blobs"
      ]
    }
  ]
}
```

**Performance:**
- Upload: ~100 MB/s (network dependent)
- Download: ~200 MB/s (network dependent)
- Latency: ~20-50ms (same region)

### 2. Azure Blob Storage Backend

**Features:**
- Server-side encryption (AES256)
- Access tiers (Hot, Cool, Archive)
- Immutable storage (WORM)
- Lifecycle management
- Geo-redundant storage (GRS, RA-GRS)

**Dependencies:**
- azure-storage-blobs-cpp >= 12.0.0
- OpenSSL >= 1.1.1

**Configuration:**

```cpp
BlobStorageConfig config;
config.enable_azure = true;
config.azure_connection_string = "DefaultEndpointsProtocol=https;AccountName=themisdb;AccountKey=...;EndpointSuffix=core.windows.net";
config.azure_container = "blobs";

// Alternative: Managed Identity
// Connection string nicht nötig bei Azure VM/App Service
```

**Plugin Manifest:**
```json
{
  "name": "azure_blob_storage",
  "type": "blob_storage",
  "binary": {
    "linux": "themis_blob_azure.so"
  },
  "configuration": {
    "container": "themisdb-blobs"
  }
}
```

**Usage:**
```cpp
// Via PluginManager
auto& pm = PluginManager::instance();
auto* plugin = pm.loadPlugin("azure_blob_storage");
auto* backend = static_cast<IBlobStorageBackend*>(plugin->getInstance());

// Store blob
std::vector<uint8_t> data = {...};
auto ref = backend->put("blob_456", data);
// ref.uri = "azure://themisdb-blobs/blob_456.blob"

// Retrieve blob
auto retrieved = backend->get(ref);
```

**Azure Container Configuration:**
```bash
# Azure CLI
az storage container create \
  --name themisdb-blobs \
  --account-name themisdb \
  --public-access off \
  --encryption-scope themis-encryption

# Set lifecycle policy (auto-archive after 90 days)
az storage account management-policy create \
  --account-name themisdb \
  --policy @lifecycle-policy.json
```

**Performance:**
- Upload: ~80 MB/s (network dependent)
- Download: ~150 MB/s (network dependent)
- Latency: ~25-60ms (same region)

## BlobStorageManager Integration

Der BlobStorageManager wählt automatisch das beste Backend basierend auf Konfiguration und Schwellwerten:

```cpp
BlobStorageManager manager(config);

// Automatische Backend-Auswahl:
// < 1 MB     → INLINE (RocksDB)
// 1-10 MB    → ROCKSDB_BLOB
// > 10 MB    → External (S3/Azure/Filesystem/WebDAV)

auto ref = manager.put("large_document", data);  // Automatisch zu S3/Azure
auto retrieved = manager.get(ref);
```

**Backend-Priorisierung:**
1. **S3** - Wenn `enable_s3 = true` (bevorzugt für Cloud-Deployments)
2. **Azure** - Wenn `enable_azure = true` und kein S3
3. **WebDAV** - Wenn `enable_webdav = true` (für ActiveDirectory)
4. **Filesystem** - Fallback (immer verfügbar)

## Deployment-Szenarien

### Scenario 1: AWS Deployment
```cpp
BlobStorageConfig config;
config.enable_s3 = true;
config.s3_bucket = "prod-themisdb-blobs";
config.s3_region = "eu-central-1";
config.enable_filesystem = false;  // Nur S3

// IAM Role automatisch via EC2 Instance Profile
```

### Scenario 2: Azure Deployment
```cpp
BlobStorageConfig config;
config.enable_azure = true;
config.azure_container = "prod-blobs";
config.enable_filesystem = false;  // Nur Azure

// Managed Identity automatisch via App Service
```

### Scenario 3: Hybrid (Multi-Cloud)
```cpp
BlobStorageConfig config;
config.enable_s3 = true;           // Primary
config.enable_azure = true;        // Backup/DR
config.enable_filesystem = true;   // Local cache

// BlobStorageManager nutzt S3 primär, Azure für Redundanz
```

### Scenario 4: On-Premise + Cloud Backup
```cpp
BlobStorageConfig config;
config.enable_filesystem = true;   // Primary (on-premise)
config.filesystem_base_path = "/mnt/themisdb/blobs";
config.enable_s3 = true;           // Backup (S3 Glacier)
config.s3_bucket = "backup-themisdb";
```

## Cost Optimization

### S3 Costs
- Storage: $0.023/GB/month (Standard)
- PUT: $0.005/1000 requests
- GET: $0.0004/1000 requests
- **Lifecycle:** Move to Glacier after 90 days → $0.004/GB/month

### Azure Costs
- Storage: $0.018/GB/month (Hot tier)
- PUT: $0.05/10,000 operations
- GET: $0.004/10,000 operations
- **Lifecycle:** Move to Cool tier after 30 days → $0.01/GB/month

### Beispielrechnung (1 TB Daten, 100k Requests/Monat)
- **S3:** ~$25/month (Standard) | ~$6/month (Glacier)
- **Azure:** ~$20/month (Hot) | ~$12/month (Cool)
- **Filesystem:** Server-Kosten (variabel)

## Security

### Encryption at Rest
- **S3:** AES-256 (server-side), optional KMS
- **Azure:** AES-256 (automatisch), optional Customer-Managed Keys

### Encryption in Transit
- **S3:** HTTPS (TLS 1.2+)
- **Azure:** HTTPS (TLS 1.2+)

### Access Control
- **S3:** IAM Policies, Bucket Policies, ACLs
- **Azure:** Azure AD, RBAC, SAS Tokens

### Compliance
- **S3:** GDPR, HIPAA, SOC 2, ISO 27001
- **Azure:** GDPR, HIPAA, SOC 2, ISO 27001

## Monitoring

### S3 CloudWatch Metrics
```bash
# Enable detailed monitoring
aws s3api put-bucket-metrics-configuration \
  --bucket themisdb-blobs \
  --id themis-metrics \
  --metrics-configuration '{
    "Id": "themis-metrics",
    "Filter": {
      "Prefix": "production/"
    }
  }'
```

### Azure Monitor
```bash
# Enable diagnostics
az monitor diagnostic-settings create \
  --name themisdb-diagnostics \
  --resource /subscriptions/.../storageAccounts/themisdb \
  --logs '[{"category": "StorageRead", "enabled": true}]' \
  --metrics '[{"category": "Transaction", "enabled": true}]'
```

## Troubleshooting

### S3 Connection Issues
```bash
# Test AWS credentials
aws s3 ls s3://themisdb-blobs/

# Check IAM permissions
aws iam get-role-policy --role-name ThemisDBRole --policy-name S3Access
```

### Azure Connection Issues
```bash
# Test connection
az storage blob list \
  --container-name themisdb-blobs \
  --account-name themisdb

# Verify connection string
az storage account show-connection-string --name themisdb
```

## Migration

### Filesystem → S3
```cpp
// Migrate existing blobs to S3
BlobMigrator migrator;
migrator.migrate(
    source: filesystem_backend,
    destination: s3_backend,
    batch_size: 1000,
    verify_hashes: true
);
```

### S3 → Azure (Cross-Cloud)
```bash
# Use rclone for bulk migration
rclone sync s3:themisdb-blobs azure:themisdb-blobs \
  --transfers 50 \
  --checkers 20 \
  --progress
```

## Status

✅ **S3 Backend** - Fully implemented (blob_backend_s3.cpp)  
✅ **Azure Backend** - Fully implemented (blob_backend_azure.cpp)  
✅ **Plugin Manifests** - Signed and ready  
✅ **BlobStorageManager Integration** - Auto-selection logic  
✅ **Documentation** - Complete with examples
