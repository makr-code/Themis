# Encryption Metrics Documentation

## Overview

ThemisDB exposes comprehensive encryption metrics for monitoring security operations, performance, and key rotation progress.

## Metrics Structure

### Operation Counters

**`themis_encryption_operations_total`** (Counter)
- **Description**: Total number of encryption operations
- **Labels**: None
- **Use Case**: Track encryption workload

**`themis_decryption_operations_total`** (Counter)
- **Description**: Total number of decryption operations
- **Labels**: None
- **Use Case**: Track decryption workload

**`themis_reencryption_operations_total`** (Counter)
- **Description**: Total number of successful lazy re-encryptions
- **Labels**: None
- **Use Case**: Monitor key rotation progress

**`themis_reencryption_skipped_total`** (Counter)
- **Description**: Number of re-encryption checks that found data already using latest key
- **Labels**: None
- **Use Case**: Identify completion of key rotation

---

### Error Counters

**`themis_encryption_errors_total`** (Counter)
- **Description**: Total number of encryption failures
- **Labels**: None
- **Alerts**: Spike indicates key provider issues or memory exhaustion

**`themis_decryption_errors_total`** (Counter)
- **Description**: Total number of decryption failures
- **Labels**: None
- **Alerts**: Non-zero indicates data corruption, key mismatch, or tampering attempts

**`themis_reencryption_errors_total`** (Counter)
- **Description**: Total number of lazy re-encryption failures
- **Labels**: None
- **Alerts**: Non-zero indicates key rotation issues

---

### Performance Metrics

**`themis_encryption_duration_seconds`** (Histogram)
- **Description**: Encryption operation latency distribution
- **Buckets**:
  - `le_100us`: ≤ 100 microseconds
  - `le_500us`: ≤ 500 microseconds
  - `le_1ms`: ≤ 1 millisecond
  - `le_5ms`: ≤ 5 milliseconds
  - `le_10ms`: ≤ 10 milliseconds
  - `gt_10ms`: > 10 milliseconds
- **Use Case**: Detect performance degradation

**`themis_decryption_duration_seconds`** (Histogram)
- **Description**: Decryption operation latency distribution
- **Buckets**: Same as encryption
- **Use Case**: Monitor read path latency

---

### Data Volume Metrics

**`themis_encryption_bytes_total`** (Counter)
- **Description**: Total bytes encrypted
- **Labels**: None
- **Use Case**: Storage capacity planning, compliance reporting

**`themis_decryption_bytes_total`** (Counter)
- **Description**: Total bytes decrypted
- **Labels**: None
- **Use Case**: Read workload analysis

---

### Key Rotation Metrics

**`themis_key_rotation_events_total`** (Counter)
- **Description**: Total number of key rotation events
- **Labels**: None
- **Use Case**: Audit key lifecycle

**Key Rotation Progress** (Derived Metric)
- **Formula**: `reencrypt_skipped / (reencrypt_operations + reencrypt_skipped) * 100`
- **Description**: Percentage of data already using latest key version
- **Target**: 100% (all data migrated)

---

## Access via HTTP API

### GET `/api/metrics`

Returns all metrics in Prometheus exposition format:

```
# TYPE themis_encryption_operations_total counter
themis_encryption_operations_total 1234567

# TYPE themis_decryption_operations_total counter
themis_decryption_operations_total 9876543

# TYPE themis_reencryption_operations_total counter
themis_reencryption_operations_total 45678

# TYPE themis_encryption_duration_le_1ms counter
themis_encryption_duration_le_1ms 1200000

# TYPE themis_encryption_bytes_total counter
themis_encryption_bytes_total 52428800
```

### GET `/api/encryption/metrics` (JSON)

Returns encryption-specific metrics as JSON:

```json
{
  "operations": {
    "encrypt_total": 1234567,
    "decrypt_total": 9876543,
    "reencrypt_total": 45678,
    "reencrypt_skipped": 2345
  },
  "errors": {
    "encrypt_errors": 0,
    "decrypt_errors": 2,
    "reencrypt_errors": 0
  },
  "performance": {
    "encrypt_duration_buckets": {
      "le_100us": 800000,
      "le_500us": 350000,
      "le_1ms": 50000,
      "le_5ms": 30000,
      "le_10ms": 3500,
      "gt_10ms": 1067
    },
    "decrypt_duration_buckets": {
      "le_100us": 7000000,
      "le_500us": 2500000,
      "le_1ms": 200000,
      "le_5ms": 150000,
      "le_10ms": 20000,
      "gt_10ms": 6543
    }
  },
  "bytes": {
    "encrypted_total": 52428800,
    "decrypted_total": 419430400
  },
  "key_rotation": {
    "rotation_events": 3,
    "migration_progress_percent": 95.2
  }
}
```

---

## Grafana Dashboard Queries

### Encryption Operations Rate

```promql
rate(themis_encryption_operations_total[5m])
```

### Decryption Error Rate

```promql
rate(themis_decryption_errors_total[5m])
```

### Key Rotation Progress

```promql
100 * (
  themis_reencryption_skipped_total / 
  (themis_reencryption_operations_total + themis_reencryption_skipped_total)
)
```

### P95 Encryption Latency

```promql
histogram_quantile(0.95, 
  rate(themis_encryption_duration_le_1ms[5m])
)
```

### Encryption Throughput (MB/s)

```promql
rate(themis_encryption_bytes_total[5m]) / 1024 / 1024
```

---

## Alerts

### Critical Alerts

**HighDecryptionErrorRate**
```yaml
alert: HighDecryptionErrorRate
expr: rate(themis_decryption_errors_total[5m]) > 0.01
for: 5m
severity: critical
annotations:
  summary: "Decryption error rate > 1%"
  description: "Data corruption or tampering detected"
```

**EncryptionPerformanceDegradation**
```yaml
alert: EncryptionPerformanceDegradation
expr: themis_encryption_duration_gt_10ms / themis_encryption_operations_total > 0.05
for: 10m
severity: warning
annotations:
  summary: "> 5% of encryptions take > 10ms"
  description: "Key provider latency or resource exhaustion"
```

### Warning Alerts

**SlowKeyRotation**
```yaml
alert: SlowKeyRotation
expr: |
  100 * (
    themis_reencryption_skipped_total / 
    (themis_reencryption_operations_total + themis_reencryption_skipped_total)
  ) < 50
for: 24h
severity: warning
annotations:
  summary: "Key rotation < 50% complete after 24h"
  description: "Increase re-encryption rate or check errors"
```

---

## Implementation Details

### Thread Safety

All metrics use `std::atomic` with `memory_order_relaxed` for lock-free updates. This ensures:
- Zero contention on hot paths
- Consistent reads (eventual consistency)
- No performance impact on encryption operations

### Memory Overhead

Total memory per `FieldEncryption` instance:
- 42 counters × 8 bytes = 336 bytes
- Negligible overhead (<0.01% of typical workload)

### Metric Collection

Metrics are collected at:
- **Encryption Path**: `encrypt()` entry/exit
- **Decryption Path**: `decryptToBytes()` entry/exit
- **Re-Encryption Path**: `decryptAndReEncrypt()` decision points

Duration tracking uses `std::chrono::high_resolution_clock` with microsecond precision.

---

## Example: Monitoring Key Rotation

**Scenario**: Rotate `user_pii` key from v2 to v3

1. **Before Rotation**:
   ```
   reencrypt_operations_total = 0
   reencrypt_skipped_total = 0
   ```

2. **During Rotation** (first 1000 reads):
   ```
   reencrypt_operations_total = 1000
   reencrypt_skipped_total = 0
   migration_progress = 0%
   ```

3. **Mid-Rotation** (50% complete):
   ```
   reencrypt_operations_total = 50000
   reencrypt_skipped_total = 50000
   migration_progress = 50%
   ```

4. **After Rotation** (all data migrated):
   ```
   reencrypt_operations_total = 100000
   reencrypt_skipped_total = 900000
   migration_progress = 90%
   
   # Next reads only increment skipped:
   reencrypt_skipped_total = 1000000
   migration_progress = 90.9%
   ```

---

## Compliance Reporting

### GDPR Article 32 (Security of Processing)

**Requirement**: Demonstrate encryption of personal data

**Evidence**:
- `themis_encryption_operations_total > 0` (encryption active)
- `themis_decryption_errors_total == 0` (integrity verified)
- `themis_encryption_bytes_total` (volume of encrypted data)

### eIDAS Regulation (EU 910/2014)

**Requirement**: Key rotation within 12 months

**Evidence**:
- `themis_key_rotation_events_total >= 1` (per year per key)
- `migration_progress == 100%` (all data migrated)
- Grafana dashboard: Time-to-100% migration < 30 days

---

## Troubleshooting

### Problem: High `decrypt_errors_total`

**Causes**:
1. **Data Corruption**: Disk/network errors
   - Check: RocksDB metrics, disk SMART status
2. **Key Mismatch**: Wrong key version after restore
   - Check: Vault key version consistency
3. **Tampering**: Authentication tag failures
   - Check: Audit logs for unauthorized access

**Resolution**:
```bash
# Check error details in logs
grep "Decryption failed" server.err | tail -20

# Verify key provider connectivity
curl -k https://vault:8200/v1/sys/health

# Test decryption with known good blob
./test_encryption --verify-blob "known_good.json"
```

### Problem: Slow Key Rotation

**Causes**:
1. **Low Read Rate**: Data rarely accessed
   - Solution: Proactive bulk re-encryption
2. **Re-Encryption Errors**: `reencrypt_errors_total > 0`
   - Check: Key provider availability, memory

**Resolution**:
```bash
# Force re-encryption of all data
./admin_tool reencrypt --collection users --field email --key-id user_pii

# Monitor progress
curl http://localhost:8080/api/encryption/metrics | jq '.key_rotation.migration_progress_percent'
```

---

## Future Enhancements

### Planned Metrics (v2.0)

1. **Per-Key Metrics**:
   - `themis_encryption_operations_total{key_id="user_pii"}`
   - Requires thread-safe map or metric registry

2. **Field-Level Metrics**:
   - `themis_encrypted_fields_total{collection="users",field="email"}`
   - Track schema-based encryption coverage

3. **HSM Integration Metrics**:
   - `themis_hsm_operations_total{operation="sign|verify|encrypt"}`
   - Monitor hardware security module usage

4. **Cache Hit Rate**:
   - `themis_key_cache_hits / themis_key_cache_total`
   - Optimize key provider caching

---

## References

- [Prometheus Best Practices](https://prometheus.io/docs/practices/naming/)
- [OpenMetrics Specification](https://github.com/OpenObservability/OpenMetrics)
- [ThemisDB Security Architecture](./pki_integration_architecture.md)
- [Key Rotation Guide](./key_rotation.md)
