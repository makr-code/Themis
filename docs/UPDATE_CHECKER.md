# Update Checker Subsystem

## Overview

The Update Checker subsystem is a DLL/shared library component that periodically checks GitHub for new ThemisDB releases and makes this information available via the HTTP API.

## Architecture

### Components

1. **UpdateChecker** (`utils/update_checker.h/cpp`)
   - Core class implementing version checking logic
   - Periodic background polling of GitHub Releases API
   - Semantic versioning comparison
   - Thread-safe implementation

2. **UpdateApiHandler** (`server/update_api_handler.h/cpp`)
   - HTTP API endpoints for update status
   - RESTful interface for admin tools

3. **Integration** (`server/http_server.h/cpp`)
   - Integrated into main HTTP server
   - Feature flag: `feature_update_checker`

### Design Principles

- **OOP Design**: Clean separation of concerns with dedicated classes
- **Thread Safety**: All public methods are thread-safe using mutexes
- **Graceful Degradation**: Works without CURL (returns error message)
- **Configurable**: All parameters configurable via config or environment variables
- **Non-intrusive**: Minimal changes to existing codebase

## Features

### Version Comparison
- **Semantic Versioning**: Full support for semver (major.minor.patch)
- **Prerelease Support**: Handles alpha, beta, rc versions
- **Tag Parsing**: Parses tags like "v1.2.3" or "1.2.3"

### Release Detection
- **Critical Patches**: Automatically identifies security/critical updates
  - Keywords: security, critical, vulnerability, CVE, exploit, patch, urgent, hotfix
- **Prerelease Filtering**: Option to include/exclude prereleases

### Update Checking
- **Periodic Checking**: Background thread checks at configured interval (default: 1 hour)
- **Manual Trigger**: API endpoint to trigger immediate check
- **GitHub API**: Uses GitHub Releases API
- **Rate Limiting**: Supports GitHub API token for higher rate limits

## HTTP API

### GET /api/updates

Get current update status.

**Response:**
```json
{
  "status": "update_available",
  "message": "A new version is available",
  "current_version": "1.0.0",
  "latest_release": {
    "version": "1.2.0",
    "tag_name": "v1.2.0",
    "name": "Release 1.2.0",
    "published_at": "2025-01-15T10:00:00Z",
    "url": "https://github.com/makr-code/ThemisDB/releases/tag/v1.2.0",
    "prerelease": false
  },
  "last_check_time": "2025-01-20T14:30:00Z"
}
```

**Status Values:**
- `up_to_date`: Running the latest version
- `update_available`: New version available
- `critical_update`: Critical security update available
- `check_failed`: Failed to check for updates
- `checking`: Currently checking
- `unknown`: Not yet checked

### POST /api/updates/check

Trigger manual update check (blocking).

**Response:** Same as GET /api/updates

### GET /api/updates/config

Get update checker configuration.

**Response:**
```json
{
  "github_owner": "makr-code",
  "github_repo": "ThemisDB",
  "current_version": "1.0.0",
  "check_interval_seconds": 3600,
  "auto_update_enabled": false,
  "auto_update_critical_only": true,
  "github_api_url": "https://api.github.com",
  "is_running": true
}
```

### PUT /api/updates/config

Update configuration (requires admin token).

**Request Body:**
```json
{
  "check_interval_seconds": 7200,
  "auto_update_enabled": false
}
```

## Configuration

### Server Config

Add to server configuration:

```yaml
http_server:
  feature_update_checker: true
```

### Environment Variables

- `THEMIS_GITHUB_API_TOKEN`: GitHub API token (for higher rate limits)
- `THEMIS_UPDATE_CHECK_INTERVAL`: Check interval in seconds (default: 3600)
- `THEMIS_AUTO_UPDATE_ENABLED`: Enable automatic updates (default: false)

### CMake Build

The update checker is always compiled into `themis_core` but only activated if the feature flag is enabled.

CURL dependency is optional - if CURL is not available, update checking will return an error.

## Usage Example

### Enable Update Checker

```cpp
themis::server::HttpServer::Config config;
config.feature_update_checker = true;
// ... other config

auto server = std::make_unique<themis::server::HttpServer>(
    config, storage, secondary_index, graph_index, vector_index, tx_manager
);

server->start();
```

### Query Update Status (curl)

```bash
# Get current status
curl http://localhost:8765/api/updates

# Trigger manual check
curl -X POST http://localhost:8765/api/updates/check

# Get configuration
curl http://localhost:8765/api/updates/config

# Update configuration (requires admin token)
curl -X PUT http://localhost:8765/api/updates/config \
  -H "Authorization: Bearer $THEMIS_TOKEN_ADMIN" \
  -H "Content-Type: application/json" \
  -d '{"check_interval_seconds": 7200}'
```

## Security Considerations

### Authentication
- Update status (GET) is public (no authentication required)
- Configuration changes (PUT) require admin token
- Future: Apply endpoint may require additional verification

### HTTPS/TLS
- Recommended: Use HTTPS when querying GitHub API
- Update checker supports proxy configuration

### Token Storage
- GitHub API token should be stored securely in environment variable
- Token is never exposed in API responses (masked as ***)

## Future Enhancements

### Hot-Reload (Planned)
- POST /api/updates/apply endpoint
- Download and apply updates without server restart
- Safety checks:
  - Backup before update
  - Version compatibility check
  - Signature verification
  - Rollback capability

### Update Notifications
- Callback registration for update events
- Integration with notification systems
- Email/webhook notifications for critical updates

### Update History
- Track update history
- Audit log for applied updates
- Rollback to previous versions

## Testing

### Unit Tests
```bash
# Run update checker tests
./build/themis_tests --gtest_filter="UpdateChecker*"
```

### Integration Tests
```bash
# Test with mock GitHub API
./scripts/test_update_checker.sh
```

### Manual Testing
```bash
# Start server with update checker enabled
./build/themis_server --config config.yaml &

# Check status
curl http://localhost:8765/api/updates

# Trigger check
curl -X POST http://localhost:8765/api/updates/check
```

## Troubleshooting

### CURL Not Found
**Error:** "CURL support not enabled - cannot fetch releases"

**Solution:**
- Install CURL development package: `sudo apt-get install libcurl4-openssl-dev`
- Rebuild with CURL support

### Rate Limiting
**Error:** "HTTP error: 403"

**Solution:**
- Add GitHub API token: `export THEMIS_GITHUB_API_TOKEN=<your_token>`
- Rate limits: 60 req/hour (unauthenticated), 5000 req/hour (authenticated)

### Network Issues
**Error:** "CURL error: Couldn't resolve host"

**Solution:**
- Check network connectivity
- Configure proxy if needed via environment variable

## References

- [GitHub Releases API](https://docs.github.com/en/rest/releases/releases)
- [Semantic Versioning](https://semver.org/)
- [CURL Documentation](https://curl.se/libcurl/)
