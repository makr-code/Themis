# Hot-Reload API Documentation

## Overview

The Hot-Reload system allows ThemisDB to be updated without downtime by:
1. Downloading new release files from GitHub
2. Verifying integrity with SHA-256 hashes and signatures
3. Creating automatic backups
4. Atomically replacing files
5. Rolling back on failure

## API Endpoints

### 1. Get Manifest

Get the manifest for a specific version.

```http
GET /api/updates/manifests/:version
```

**Example:**
```bash
curl http://localhost:8765/api/updates/manifests/1.2.0
```

**Response:**
```json
{
  "version": "1.2.0",
  "tag_name": "v1.2.0",
  "release_notes": "Security fixes...",
  "is_critical": true,
  "files": [
    {
      "path": "bin/themis_server",
      "type": "executable",
      "sha256_hash": "e3b0c44...",
      "size_bytes": 1024000,
      "platform": "linux",
      "architecture": "x64",
      "download_url": "https://..."
    }
  ],
  "manifest_hash": "abc123...",
  "signature": "...",
  "build_commit": "abc123"
}
```

### 2. Download Release

Download all files for a release.

```http
POST /api/updates/download/:version
```

**Example:**
```bash
curl -X POST http://localhost:8765/api/updates/download/1.2.0
```

**Response:**
```json
{
  "success": true,
  "version": "1.2.0",
  "download_path": "/tmp/themis_updates/1.2.0",
  "manifest": { ... }
}
```

### 3. Apply Hot-Reload

Apply a hot-reload update. **Requires admin authentication.**

```http
POST /api/updates/apply/:version
```

**Request Body (optional):**
```json
{
  "verify_only": false
}
```

**Example:**
```bash
# Dry-run (verify only)
curl -X POST http://localhost:8765/api/updates/apply/1.2.0 \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"verify_only": true}'

# Actual apply
curl -X POST http://localhost:8765/api/updates/apply/1.2.0 \
  -H "Authorization: Bearer $ADMIN_TOKEN"
```

**Response:**
```json
{
  "success": true,
  "version": "1.2.0",
  "verify_only": false,
  "files_updated": [
    "bin/themis_server",
    "lib/themis_core.so"
  ],
  "rollback_id": "rollback_1234567890"
}
```

### 4. Rollback

Rollback to a previous version. **Requires admin authentication.**

```http
POST /api/updates/rollback/:rollback_id
```

**Example:**
```bash
curl -X POST http://localhost:8765/api/updates/rollback/rollback_1234567890 \
  -H "Authorization: Bearer $ADMIN_TOKEN"
```

**Response:**
```json
{
  "success": true,
  "rollback_id": "rollback_1234567890"
}
```

### 5. List Rollback Points

List all available rollback points.

```http
GET /api/updates/rollback
```

**Example:**
```bash
curl http://localhost:8765/api/updates/rollback
```

**Response:**
```json
{
  "rollback_points": [
    {
      "rollback_id": "rollback_1234567890",
      "timestamp": "2025-01-20T10:30:00Z"
    },
    {
      "rollback_id": "rollback_1234567800",
      "timestamp": "2025-01-19T15:20:00Z"
    }
  ],
  "count": 2
}
```

## Complete Workflow Example

### 1. Check for Updates
```bash
# Check if updates are available
curl http://localhost:8765/api/updates

# Response shows new version available
{
  "status": "update_available",
  "current_version": "1.0.0",
  "latest_release": {
    "version": "1.2.0",
    ...
  }
}
```

### 2. Download the Update
```bash
# Download release files
curl -X POST http://localhost:8765/api/updates/download/1.2.0

# Wait for download to complete
{
  "success": true,
  "download_path": "/tmp/themis_updates/1.2.0"
}
```

### 3. Verify Before Applying
```bash
# Dry-run to verify
curl -X POST http://localhost:8765/api/updates/apply/1.2.0 \
  -H "Authorization: Bearer $ADMIN_TOKEN" \
  -d '{"verify_only": true}'

# If verification passes
{
  "success": true,
  "verify_only": true
}
```

### 4. Apply the Update
```bash
# Apply hot-reload
curl -X POST http://localhost:8765/api/updates/apply/1.2.0 \
  -H "Authorization: Bearer $ADMIN_TOKEN"

# Update applied
{
  "success": true,
  "files_updated": ["bin/themis_server", ...],
  "rollback_id": "rollback_1234567890"
}
```

### 5. Rollback if Needed
```bash
# If something goes wrong, rollback
curl -X POST http://localhost:8765/api/updates/rollback/rollback_1234567890 \
  -H "Authorization: Bearer $ADMIN_TOKEN"
```

## Configuration

### Server Config

Enable hot-reload in server configuration:

```yaml
http_server:
  feature_update_checker: true  # Enable update checker
  feature_hot_reload: true      # Enable hot-reload (future)
```

### Environment Variables

```bash
# Update checker
export THEMIS_GITHUB_API_TOKEN=ghp_xxxxx
export THEMIS_UPDATE_CHECK_INTERVAL=3600

# Hot-reload directories
export THEMIS_DOWNLOAD_DIR=/tmp/themis_updates
export THEMIS_BACKUP_DIR=/var/lib/themisdb/rollback
```

## Security

### Authentication

- **Public endpoints**: GET /api/updates/manifests/:version, GET /api/updates/rollback
- **Protected endpoints**: POST /api/updates/apply/:version, POST /api/updates/rollback/:id

Protected endpoints require:
- Admin token in Authorization header
- Scope: `admin` or `update:apply`

### Verification

All files are verified with:
1. **SHA-256 hash** - File integrity
2. **CMS signature** - Authenticity (if manifest has signature)
3. **Size check** - Completeness
4. **Manifest hash** - Overall integrity

### Backup & Rollback

- Automatic backup created before every update
- Atomic file replacement prevents partial updates
- Rollback points kept for configurable retention period
- Default: Keep last 3 rollback points

## Error Handling

### Common Errors

**Download Failed:**
```json
{
  "success": false,
  "error": "Failed to download file: bin/themis_server"
}
```

**Verification Failed:**
```json
{
  "success": false,
  "error": "Hash mismatch for file: bin/themis_server"
}
```

**Incompatible Upgrade:**
```json
{
  "success": false,
  "error": "Incompatible upgrade from 0.9.0 to 1.2.0"
}
```

**Manifest Not Found:**
```json
{
  "error": "Manifest not found for version: 1.2.0",
  "status": 404
}
```

## Integration with Update Checker

The hot-reload system integrates seamlessly with the update checker:

```bash
# 1. Update checker detects new version
GET /api/updates
# -> "status": "critical_update"

# 2. Download automatically or manually
POST /api/updates/download/1.2.0

# 3. Apply critical patch (can be automated)
POST /api/updates/apply/1.2.0
```

For critical security updates, the system can be configured to:
- Auto-download on detection
- Auto-apply critical patches
- Send notifications before applying

## Monitoring

### Progress Callbacks

The hot-reload engine supports progress callbacks for monitoring:

```cpp
reload_engine->setProgressCallback([](int percentage, const std::string& message) {
    LOG_INFO("Progress: {}% - {}", percentage, message);
    // Send to monitoring system
});
```

### Metrics

Track hot-reload metrics:
- Number of successful updates
- Number of rollbacks
- Average download time
- Average apply time
- Failed updates

## Best Practices

1. **Always verify first** - Use `verify_only=true` before applying
2. **Monitor disk space** - Download directory needs space for release files
3. **Keep rollback points** - Don't clean too aggressively
4. **Test in staging** - Apply updates to staging environment first
5. **Schedule maintenance** - Apply non-critical updates during low traffic
6. **Auto-apply critical** - Consider auto-applying security patches
7. **Monitor logs** - Check logs after applying updates

## Limitations

Current limitations:
- Cannot update running server binary (requires restart)
- No support for database schema migrations
- Rollback doesn't restore database state
- CURL required for downloads
- No multi-node coordination yet

Future enhancements planned:
- Graceful restart after update
- Database migration support
- Multi-node rolling updates
- WebSocket progress streaming
- Update scheduling
