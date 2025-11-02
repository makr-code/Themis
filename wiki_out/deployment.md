# THEMIS Deployment Guide

## Table of Contents

1. [System Requirements](#system-requirements)
2. [Installation Methods](#installation-methods)
3. [Configuration](#configuration)
4. [Production Deployment](#production-deployment)
5. [Monitoring & Observability](#monitoring--observability)
6. [Backup & Recovery](#backup--recovery)
7. [Performance Tuning](#performance-tuning)
8. [Security](#security)
9. [Troubleshooting](#troubleshooting)

## System Requirements

### Minimum Requirements

- **CPU**: 4 cores (x86_64 or ARM64)
- **RAM**: 2 GB (1 GB for RocksDB, 1 GB for system/buffers)
- **Disk**: 20 GB SSD (NVMe recommended)
- **OS**: Windows 10/11, Linux (Ubuntu 20.04+, RHEL 8+), macOS 12+
- **Network**: 1 Gbps (for distributed deployments)

### Recommended Requirements

- **CPU**: 8+ cores (Intel Xeon, AMD EPYC, or Apple Silicon M-series)
- **RAM**: 8 GB (4 GB block cache, 1 GB memtable, 3 GB system)
- **Disk**: 100 GB+ NVMe SSD (read: 3000 MB/s, write: 1500 MB/s)
- **OS**: Linux (kernel 5.10+) for production workloads
- **Network**: 10 Gbps (low-latency network for replication)

## Installation Methods

### Method 1: Binary Release (Recommended for Production)

```bash
# Download latest release
wget https://github.com/<org>/vccdb/releases/download/v1.0.0/vccdb-linux-x64.tar.gz

# Extract
tar -xzf vccdb-linux-x64.tar.gz
cd vccdb

# Verify installation
./themis_server --version
# Output: THEMIS v1.0.0 (build: 2025-10-28, commit: abc1234)
```

### Method 2: Docker (Recommended for Containers)

```bash
# Pull image
docker pull vccdb/vccdb:latest

# Run with persistent storage
docker run -d \
  --name vccdb \
  -p 8765:8765 \
  -v $(pwd)/data:/data \
  -v $(pwd)/config.json:/etc/vccdb/config.json \
  vccdb/vccdb:latest

# Check logs
docker logs -f vccdb
```

### Method 3: Build from Source (Development)

```powershell
# Windows (PowerShell)
git clone https://github.com/<org>/vccdb.git
cd vccdb
.\setup.ps1      # Install vcpkg dependencies
.\build.ps1      # Build Release binaries
```

```bash
# Linux/macOS (Bash)
git clone https://github.com/<org>/vccdb.git
cd vccdb
./setup.sh       # Install dependencies
./build.sh       # Build Release binaries
```

## Configuration

### Basic Configuration (`config.json`)

```json
{
  "storage": {
    "rocksdb_path": "/var/lib/vccdb/data",
    "memtable_size_mb": 256,
    "block_cache_size_mb": 1024,
    "max_open_files": 10000,
    "enable_statistics": true,
    "compression": {
      "default": "lz4",
      "bottommost": "zstd"
    }
  },
  "server": {
    "host": "0.0.0.0",
    "port": 8765,
    "worker_threads": 8,
    "request_timeout_ms": 30000,
    "max_request_size_mb": 10
  },
  "logging": {
    "level": "info",
    "file": "/var/log/vccdb/server.log",
    "rotation_size_mb": 100,
    "max_files": 10
  },
  "vector_index": {
    "engine": "hnsw",
    "hnsw_m": 16,
    "hnsw_ef_construction": 200,
    "use_gpu": false
  }
}
```

### Environment Variables

Override config values with environment variables:

```bash
export THEMIS_SERVER_PORT=9000
export THEMIS_STORAGE_PATH=/mnt/nvme/vccdb
export THEMIS_LOG_LEVEL=debug
export THEMIS_WORKER_THREADS=16

./themis_server --config config.json
# Port 9000, custom storage path, debug logging, 16 threads
```

### Configuration Validation

```bash
# Validate config before starting server
./themis_server --config config.json --validate

# Output:
# ✓ Config file valid
# ✓ Storage path accessible: /var/lib/vccdb/data
# ✓ Port 8765 available
# ✓ Memory limits: memtable (256 MB) + block_cache (1024 MB) = 1280 MB
# ✓ Worker threads: 8 (optimal for 8-core CPU)
```

### Runtime Configuration (Hot-Reload)

THEMIS supports **hot-reload** for specific configuration values without requiring a server restart.

#### Supported Hot-Reload Settings

The following settings can be updated at runtime via `POST /config`:

1. **Logging Configuration**
   - `logging.level`: "trace", "debug", "info", "warn", "error"
   - `logging.format`: "text" (human-readable), "json" (structured logs for aggregation)

2. **Request Timeout**
   - `request_timeout_ms`: 1000-300000 (1 second to 5 minutes)

3. **Feature Flags** (Beta features)
   - `features.semantic_cache`: Enable/disable semantic query caching
   - `features.llm_store`: Enable/disable LLM interaction storage
   - `features.cdc`: Enable/disable Change Data Capture streaming
   - `features.timeseries`: Enable/disable time-series data store

4. **CDC Retention Policy** (Logging only - requires manual cleanup)
   - `cdc_retention_hours`: 1-8760 (1 hour to 1 year)

#### Hot-Reload Examples

**Example 1: Enable JSON Logging**

```bash
curl -X POST http://localhost:8765/config \
  -H "Content-Type: application/json" \
  -d '{
    "logging": {
      "level": "info",
      "format": "json"
    }
  }'

# Response: Updated config with all current settings
# Server logs now output structured JSON
```

**Example 2: Update Request Timeout**

```bash
curl -X POST http://localhost:8765/config \
  -H "Content-Type: application/json" \
  -d '{
    "request_timeout_ms": 60000
  }'

# Timeout increased to 60 seconds for long-running queries
```

**Example 3: Enable CDC Feature**

```bash
curl -X POST http://localhost:8765/config \
  -H "Content-Type: application/json" \
  -d '{
    "features": {
      "cdc": true
    }
  }'

# CDC streaming endpoints now accessible: /changefeed/stream
```

**Example 4: Multiple Settings at Once**

```bash
curl -X POST http://localhost:8765/config \
  -H "Content-Type: application/json" \
  -d '{
    "logging": {
      "level": "debug",
      "format": "json"
    },
    "request_timeout_ms": 45000,
    "features": {
      "semantic_cache": true,
      "cdc": true
    }
  }'
```

#### Limitations

**Cannot be changed at runtime (requires restart):**

- `server.port`: HTTP server port
- `server.threads`: Number of worker threads (thread pool is fixed at startup)
- `rocksdb.*`: All RocksDB storage settings (memtable size, block cache, compression, etc.)
- `vector.dimension`: Vector index dimensionality
- Data directory paths

**Validation Rules:**

- `request_timeout_ms`: Must be between 1000 and 300000 (1s-5min)
- `cdc_retention_hours`: Must be between 1 and 8760 (1h-1yr)
- Feature flags: Boolean values only

#### Verify Current Configuration

```bash
# GET current config
curl http://localhost:8765/config | jq .

# Output includes all settings (read-only + hot-reload capable)
{
  "server": {
    "port": 8765,
    "threads": 8,
    "request_timeout_ms": 30000
  },
  "features": {
    "semantic_cache": false,
    "llm_store": false,
    "cdc": true,
    "timeseries": false
  },
  "rocksdb": {
    "db_path": "/var/lib/vccdb/data",
    "memtable_size_mb": 256,
    ...
  }
}
```

## Production Deployment

### Systemd Service (Linux)

Create `/etc/systemd/system/vccdb.service`:

```ini
[Unit]
Description=THEMIS Multi-Model Database Server
After=network.target

[Service]
Type=simple
User=vccdb
Group=vccdb
WorkingDirectory=/opt/vccdb
ExecStart=/opt/vccdb/themis_server --config /etc/vccdb/config.json
Restart=on-failure
RestartSec=10

# Security hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/lib/vccdb /var/log/vccdb

# Resource limits
LimitNOFILE=65536
MemoryMax=8G
CPUQuota=800%

[Install]
WantedBy=multi-user.target
```

```bash
# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable vccdb
sudo systemctl start vccdb

# Check status
sudo systemctl status vccdb
sudo journalctl -u vccdb -f
```

### Docker Compose (Container Orchestration)

Hinweis zu Ports:
- Der mitgelieferte `docker-compose.yml` im Repository nutzt standardmäßig Port `8080` (Mapping `8080:8080`).
- In den Beispielen dieses Guides wird Port `8765` verwendet. Passe den Port in Compose entsprechend an (oder setze `THEMIS_PORT`).

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  vccdb:
    image: vccdb/vccdb:latest
    container_name: vccdb
    ports:
  - "8765:8765"  # ggf. zu "8080:8080" anpassen, wenn Image/Entrypoint 8080 nutzt
    volumes:
      - vccdb-data:/data
      - ./config.json:/etc/vccdb/config.json:ro
    environment:
      - THEMIS_LOG_LEVEL=info
      - THEMIS_WORKER_THREADS=8
    restart: unless-stopped
    healthcheck:
      test: ["CMD", "curl", "-f", "http://localhost:8765/health"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 40s
    deploy:
      resources:
        limits:
          cpus: '8'
          memory: 8G
        reservations:
          cpus: '4'
          memory: 4G

  prometheus:
    image: prom/prometheus:latest
    ports:
      - "9090:9090"
    volumes:
      - ./prometheus.yml:/etc/prometheus/prometheus.yml:ro
      - prometheus-data:/prometheus
    command:
      - '--config.file=/etc/prometheus/prometheus.yml'
      - '--storage.tsdb.path=/prometheus'
      - '--storage.tsdb.retention.time=30d'

  grafana:
    image: grafana/grafana:latest
    ports:
      - "3000:3000"
    volumes:
      - grafana-data:/var/lib/grafana
      - ./grafana-dashboards:/etc/grafana/provisioning/dashboards:ro
    environment:
      - GF_SECURITY_ADMIN_PASSWORD=admin
      - GF_INSTALL_PLUGINS=grafana-piechart-panel

volumes:
  vccdb-data:
  prometheus-data:
  grafana-data:
```

```bash
# Start stack
docker-compose up -d

# View logs
docker-compose logs -f vccdb

# Scale (if using load balancer)
docker-compose up -d --scale vccdb=3
```

### Kubernetes Deployment

Create `k8s/deployment.yaml`:

```yaml
apiVersion: v1
kind: ConfigMap
metadata:
  name: vccdb-config
data:
  config.json: |
    {
      "storage": {
        "rocksdb_path": "/data/vccdb",
        "memtable_size_mb": 512,
        "block_cache_size_mb": 2048
      },
      "server": {
        "host": "0.0.0.0",
        "port": 8765,
        "worker_threads": 16
      }
    }
---
apiVersion: v1
kind: PersistentVolumeClaim
metadata:
  name: vccdb-data
spec:
  accessModes:
    - ReadWriteOnce
  storageClassName: fast-ssd
  resources:
    requests:
      storage: 100Gi
---
apiVersion: apps/v1
kind: Deployment
metadata:
  name: vccdb
  labels:
    app: vccdb
spec:
  replicas: 3
  selector:
    matchLabels:
      app: vccdb
  template:
    metadata:
      labels:
        app: vccdb
      annotations:
        prometheus.io/scrape: "true"
        prometheus.io/port: "8765"
        prometheus.io/path: "/metrics"
    spec:
      containers:
      - name: vccdb
        image: vccdb/vccdb:latest
        ports:
        - containerPort: 8765
          name: http
        volumeMounts:
        - name: data
          mountPath: /data
        - name: config
          mountPath: /etc/vccdb
          readOnly: true
        resources:
          requests:
            cpu: 4000m
            memory: 8Gi
          limits:
            cpu: 8000m
            memory: 16Gi
        livenessProbe:
          httpGet:
            path: /health
            port: 8765
          initialDelaySeconds: 30
          periodSeconds: 10
        readinessProbe:
          httpGet:
            path: /health
            port: 8765
          initialDelaySeconds: 5
          periodSeconds: 5
      volumes:
      - name: data
        persistentVolumeClaim:
          claimName: vccdb-data
      - name: config
        configMap:
          name: vccdb-config
---
apiVersion: v1
kind: Service
metadata:
  name: vccdb
  labels:
    app: vccdb
spec:
  type: ClusterIP
  ports:
  - port: 8765
    targetPort: 8765
    protocol: TCP
    name: http
  selector:
    app: vccdb
```

```bash
# Deploy to Kubernetes
kubectl apply -f k8s/deployment.yaml

# Check status
kubectl get pods -l app=vccdb
kubectl logs -l app=vccdb -f

# Expose externally (LoadBalancer)
kubectl expose deployment vccdb --type=LoadBalancer --port=8765
```

## Monitoring & Observability

### Reverse Proxy und SSE/Keep-Alive Hinweise

Server‑Sent Events (SSE) nutzen eine langlebige HTTP/1.1‑Verbindung mit kontinuierlichen Datenflüssen. Für stabile Streams sollten Reverse Proxies und Load Balancer speziell konfiguriert werden:

- HTTP/1.1 erzwingen und Keep‑Alive aktiv halten
- Timeouts großzügig setzen (Lese‑/Idle‑Timeout ≥ 60s)
- Pufferung und Komprimierung für SSE deaktivieren
- Sticky Sessions/Session Affinity aktivieren, wenn mehrere Backend‑Instanzen genutzt werden

Beispiele:

1) Nginx

```
location /changefeed/stream {
  proxy_http_version 1.1;
  proxy_set_header Connection "";           # Keep-Alive nicht explizit schließen
  proxy_set_header Cache-Control no-cache;
  proxy_buffering off;                       # wichtig für SSE
  gzip off;                                  # keine Komprimierung für SSE
  chunked_transfer_encoding on;
  proxy_read_timeout 120s;                   # ausreichend hoch
  proxy_send_timeout 120s;
}
```

2) HAProxy

```
frontend http
  bind *:80
  default_backend app

backend app
  option http-keep-alive
  option http-server-close          # optional je nach Setup
  timeout server  120s
  timeout client  120s
  timeout http-keep-alive 120s
  http-response set-header Cache-Control no-cache
```

3) Windows/IIS (Beispielauszug web.config)

```
<?xml version="1.0" encoding="utf-8"?>
<configuration>
  <system.webServer>
    <httpProtocol>
      <customHeaders>
        <add name="Cache-Control" value="no-cache" />
      </customHeaders>
    </httpProtocol>
    <serverRuntime frequentHitThreshold="1" frequentHitTimePeriod="00:00:10" />
    <httpCompression directory="%SystemDrive%\\inetpub\\temp\\IIS Temporary Compressed Files">
      <dynamicTypes>
        <add enabled="false" mimeType="text/event-stream" />
      </dynamicTypes>
    </httpCompression>
    <handlers>
      <add name="SSE" path="changefeed/stream" verb="GET" modules="IsapiModule" scriptProcessor="%windir%\\system32\\inetsrv\\asp.dll" resourceType="Unspecified" requireAccess="Read" />
    </handlers>
  </system.webServer>
</configuration>
```

Zusätzlich in der Anwendungspool‑Konfiguration Idle‑Timeout ≥ 2 Minuten setzen und ggf. Request Filtering Limits (z. B. responseBufferLimit) erhöhen/abschalten.

Hinweis: Detaillierte Betriebsaspekte zum CDC‑Stream siehe `docs/change_data_capture.md`.

### Prometheus Configuration

Create `prometheus.yml`:

```yaml
global:
  scrape_interval: 15s
  evaluation_interval: 15s

scrape_configs:
  - job_name: 'vccdb'
    static_configs:
      - targets: ['vccdb:8765']
    metrics_path: /metrics
```

Zusätzliche, themis-spezifische Metriken (Auszug):
- `themis_index_rebuild_count`, `themis_index_rebuild_duration_ms_total`, `themis_index_rebuild_entities_processed_total`
- `themis_index_cursor_anchor_hits_total`, `themis_index_range_scan_steps_total`
Siehe auch: `docs/index_stats_maintenance.md`.

### CDC Quick Start (Ops)

1) Feature aktivieren (config/config.json):

```json
{
  "features": { "cdc": true }
}
```

2) Endpoint prüfen:

```bash
curl "http://localhost:8765/changefeed?from_seq=0&limit=1"
```

3) Reverse Proxy für SSE konfigurieren, falls `/changefeed/stream` genutzt wird (siehe Hinweise oben) und Details in `docs/change_data_capture.md`.

### Grafana Dashboard

Key metrics to monitor:

**Server Metrics:**
- `vccdb_requests_total` (counter): Total requests
- `vccdb_errors_total` (counter): Total errors
- `vccdb_qps` (gauge): Queries per second
- `process_uptime_seconds` (gauge): Server uptime

**RocksDB Metrics:**
- `rocksdb_block_cache_usage_bytes` / `rocksdb_block_cache_capacity_bytes`: Cache utilization
- `rocksdb_estimate_num_keys`: Total entities in database
- `rocksdb_pending_compaction_bytes`: Compaction backlog
- `rocksdb_memtable_size_bytes`: Write buffer usage
- `rocksdb_files_level{level="L0...L6"}`: SST files per level

**Query Metrics:**
- Request latency (p50, p95, p99)
- Index hit rate
- Full scan fallback rate

### Alerting Rules

Create `alerts.yml`:

```yaml
groups:
  - name: vccdb_alerts
    interval: 30s
    rules:
      - alert: THEMISHighErrorRate
        expr: rate(vccdb_errors_total[5m]) > 10
        for: 5m
        labels:
          severity: critical
        annotations:
          summary: "High error rate detected"
          description: "Error rate is {{ $value }} errors/sec"

      - alert: THEMISLowCacheHitRate
        expr: |
          rocksdb_block_cache_hit / 
          (rocksdb_block_cache_hit + rocksdb_block_cache_miss) < 0.8
        for: 10m
        labels:
          severity: warning
        annotations:
          summary: "Low block cache hit rate"
          description: "Cache hit rate is {{ $value | humanizePercentage }}"

      - alert: THEMISHighCompactionBacklog
        expr: rocksdb_pending_compaction_bytes > 10737418240  # 10 GB
        for: 15m
        labels:
          severity: warning
        annotations:
          summary: "High compaction backlog"
          description: "Pending compaction: {{ $value | humanize1024 }}B"

      - alert: THEMISDown
        expr: up{job="themis"} == 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "THEMIS server is down"
          description: "THEMIS instance {{ $labels.instance }} is unreachable"
```

## Backup & Recovery

### Snapshot Backup (RocksDB Checkpoint)

```bash
# Create snapshot
curl -X POST http://localhost:8765/admin/snapshot \
  -H "Content-Type: application/json" \
  -d '{"path":"/backups/vccdb-snapshot-2025-10-28"}'

# Verify snapshot
ls -lh /backups/vccdb-snapshot-2025-10-28/
# Output: CURRENT, MANIFEST, *.sst files

# Restore from snapshot
./themis_server --restore /backups/vccdb-snapshot-2025-10-28 \
               --target /var/lib/vccdb/data
```

### Continuous Backup (WAL Archival)

```json
{
  "storage": {
    "wal_archive_path": "/backups/wal",
    "wal_ttl_seconds": 86400,  // Keep WAL for 24 hours
    "enable_wal_archival": true
  }
}
```

```bash
# Backup script (cron job: 0 */6 * * *)
#!/bin/bash
BACKUP_DIR="/backups/$(date +%Y%m%d-%H%M%S)"
cp -r /var/lib/vccdb/data "$BACKUP_DIR"
tar -czf "$BACKUP_DIR.tar.gz" "$BACKUP_DIR"
rm -rf "$BACKUP_DIR"

# Retention: keep last 7 days
find /backups -name "*.tar.gz" -mtime +7 -delete
```

### Disaster Recovery Procedure

```bash
# 1. Stop server
sudo systemctl stop vccdb

# 2. Restore data directory
tar -xzf /backups/20251028-120000.tar.gz -C /var/lib/vccdb/

# 3. Verify data integrity
./themis_server --config config.json --verify

# 4. Restart server
sudo systemctl start vccdb

# 5. Verify health
curl http://localhost:8765/health
curl http://localhost:8765/stats | jq .storage.rocksdb.estimate_num_keys
```

## Performance Tuning

### RocksDB Tuning for Workload Types

**Write-Heavy (High Ingestion Rate):**

```json
{
  "storage": {
    "memtable_size_mb": 512,
    "max_write_buffer_number": 4,
    "min_write_buffer_number_to_merge": 2,
    "level0_file_num_compaction_trigger": 4,
    "level0_slowdown_writes_trigger": 20,
    "level0_stop_writes_trigger": 36,
    "compression": "lz4"
  }
}
```

**Read-Heavy (Analytics Workload):**

```json
{
  "storage": {
    "memtable_size_mb": 128,
    "block_cache_size_mb": 4096,
    "enable_bloom_filters": true,
    "bloom_bits_per_key": 10,
    "compression": "zstd"
  }
}
```

**Balanced (Mixed Workload):**

```json
{
  "storage": {
    "memtable_size_mb": 256,
    "block_cache_size_mb": 2048,
    "compression": {
      "default": "lz4",
      "bottommost": "zstd"
    }
  }
}
```

### Query Engine Tuning

Edit `src/query/query_engine.cpp`:

```cpp
// Low-latency tuning (reduce parallelization overhead)
constexpr size_t PARALLEL_THRESHOLD = 50;   // Start parallel at 50 entities
constexpr size_t BATCH_SIZE = 25;           // Smaller batches

// High-throughput tuning (maximize CPU utilization)
constexpr size_t PARALLEL_THRESHOLD = 200;  // Less overhead
constexpr size_t BATCH_SIZE = 100;          // Larger batches
```

### Network Tuning (Linux)

```bash
# Increase TCP buffer sizes
sudo sysctl -w net.core.rmem_max=16777216
sudo sysctl -w net.core.wmem_max=16777216
sudo sysctl -w net.ipv4.tcp_rmem="4096 87380 16777216"
sudo sysctl -w net.ipv4.tcp_wmem="4096 65536 16777216"

# Enable TCP Fast Open
sudo sysctl -w net.ipv4.tcp_fastopen=3

# Increase connection queue
sudo sysctl -w net.core.somaxconn=4096
```

## Security

### Authentication (API Key)

```json
{
  "server": {
    "enable_auth": true,
    "api_keys": [
      {"key": "sk-prod-abc123...", "role": "admin"},
      {"key": "sk-readonly-xyz789...", "role": "readonly"}
    ]
  }
}
```

```bash
# Authenticated request
curl -H "Authorization: Bearer sk-prod-abc123..." \
     http://localhost:8765/entities/users:alice
```

### TLS/SSL Encryption

```json
{
  "server": {
    "enable_tls": true,
    "tls_cert": "/etc/vccdb/certs/server.crt",
    "tls_key": "/etc/vccdb/certs/server.key",
    "tls_ca": "/etc/vccdb/certs/ca.crt"
  }
}
```

```bash
# Generate self-signed certificate (testing only)
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt \
        -days 365 -nodes -subj "/CN=localhost"

# HTTPS request
curl --cacert ca.crt https://localhost:8765/health
```

### Firewall Configuration

```bash
# Allow THEMIS port (Linux - ufw)
sudo ufw allow 8765/tcp
sudo ufw enable

# Allow from specific IP only
sudo ufw allow from 10.0.1.0/24 to any port 8765

# Windows Firewall
netsh advfirewall firewall add rule name="THEMIS Server" dir=in action=allow protocol=TCP localport=8765
```

## Troubleshooting

### Common Issues

**Issue 1: Server fails to start - "Database not open"**

```bash
# Check storage path permissions
ls -ld /var/lib/vccdb/data
# Fix: sudo chown -R vccdb:vccdb /var/lib/vccdb/data

# Check disk space
df -h /var/lib/vccdb
# Fix: Clean up old data or resize volume
```

**Issue 2: High memory usage**

```bash
# Check RocksDB memory usage
curl http://localhost:8765/stats | jq '.storage.rocksdb | {
  block_cache: .block_cache_usage_bytes,
  memtable: .memtable_size_bytes,
  total_mem_tables: .cur_size_all_mem_tables_bytes
}'

# Fix: Reduce block_cache_size_mb or memtable_size_mb in config.json
```

**Issue 3: Slow queries**

```bash
# Check index usage
curl -X POST http://localhost:8765/query \
  -H "Content-Type: application/json" \
  -d '{"table":"users","predicates":[{"column":"city","value":"Berlin"}],"explain":true}'

# Output: {"plan": {"mode": "full_scan_fallback"}}
# Fix: Create index on 'city' column
curl -X POST http://localhost:8765/index/create \
  -H "Content-Type: application/json" \
  -d '{"table":"users","column":"city"}'
```

**Issue 4: Compaction backlog**

```bash
# Check compaction stats
curl http://localhost:8765/stats | jq '.storage.rocksdb | {
  pending: .estimate_pending_compaction_bytes,
  running: .num_running_compactions
}'

# Fix: Increase compaction threads
# config.json: "max_background_compactions": 8
```

### Debug Logging

Enable debug logging:

```json
{
  "logging": {
    "level": "debug",
    "file": "/var/log/vccdb/debug.log"
  }
}
```

```bash
# Tail debug log
tail -f /var/log/vccdb/debug.log

# Filter specific component
grep "QueryEngine" /var/log/vccdb/debug.log
grep "RocksDBWrapper" /var/log/vccdb/debug.log
```

### Performance Profiling

```bash
# CPU profiling (Linux - perf)
sudo perf record -F 99 -p $(pgrep themis_server) -g -- sleep 60
sudo perf report

# Memory profiling (valgrind)
valgrind --tool=massif ./themis_server --config config.json
ms_print massif.out.12345

# Network profiling (tcpdump)
sudo tcpdump -i any port 8765 -w vccdb-traffic.pcap
wireshark vccdb-traffic.pcap
```

## Migration Guide

### From Standalone to Docker

```bash
# 1. Stop standalone server
sudo systemctl stop vccdb

# 2. Copy data directory
sudo cp -r /var/lib/vccdb/data ./docker-data/

# 3. Start Docker container
docker run -d \
  --name vccdb \
  -p 8765:8765 \
  -v $(pwd)/docker-data:/data \
  vccdb/vccdb:latest

# 4. Verify data
curl http://localhost:8765/stats | jq .storage.rocksdb.estimate_num_keys
```

### From v0.x to v1.0

```bash
# 1. Backup current data
./themis_server --backup /backups/pre-upgrade-$(date +%Y%m%d)

# 2. Download v1.0 binary
wget https://github.com/<org>/vccdb/releases/download/v1.0.0/vccdb-linux-x64.tar.gz
tar -xzf vccdb-linux-x64.tar.gz

# 3. Run migration tool
./vccdb-migrate --from /var/lib/vccdb/data \
                --to /var/lib/vccdb/data-v1 \
                --version 0.9 --target 1.0

# 4. Update config.json (new format)
# See: https://github.com/<org>/vccdb/wiki/v1.0-Migration-Guide

# 5. Start v1.0 server
./themis_server --config config-v1.json
```

## Support

- **Documentation**: https://docs.vccdb.io
- **GitHub Issues**: https://github.com/<org>/vccdb/issues
- **Community Chat**: https://discord.gg/vccdb
- **Email**: support@vccdb.io
