# VCC-Veritas Adapter

FastAPI-based adapter for VCC-Veritas verification and compliance system.

## Overview

VCC-Veritas is a verification and compliance system that focuses on:
- **Data Verification** - Checksum-based integrity validation
- **Compliance Checking** - Ensuring data meets compliance requirements
- **Data Classification** - Automatic and manual data classification
- **Audit Trail** - Verifiable audit logging for compliance purposes

This adapter provides specialized endpoints for verification, compliance, and audit operations.

**NO UDS3 DEPENDENCY** - Uses direct HTTP connections to ThemisDB via the vcc_base library.

## Features

- ✅ Document verification with SHA-256 checksums (`/verify/document`)
- ✅ Compliance data verification (`/verify/compliance`)
- ✅ Audit trail recording (`/audit/record`)
- ✅ Data integrity validation (`/validate/integrity`)
- ✅ Data classification with auto-classification (`/classify/data`)
- ✅ Support for multiple classification levels
- ✅ Verification metadata tracking
- ✅ Checksum-based tamper detection

## Prerequisites

- Python 3.10+
- Running ThemisDB instance (default: http://127.0.0.1:8765)
- vcc_base library (in parent directory)

## Quick Start

### Windows PowerShell

```powershell
cd adapters\vcc_veritas

# Create virtual environment
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# Install dependencies
pip install -r requirements.txt
cd ..\vcc_base
pip install -r requirements.txt
cd ..\vcc_veritas

# Configure environment
$env:THEMIS_URL = "http://127.0.0.1:8765"
$env:LOG_LEVEL = "INFO"

# Run adapter
uvicorn app:app --host 127.0.0.1 --port 8003 --reload
```

### Linux/macOS

```bash
cd adapters/vcc_veritas

# Create virtual environment
python3 -m venv .venv
source .venv/bin/activate

# Install dependencies
pip install -r requirements.txt
cd ../vcc_base
pip install -r requirements.txt
cd ../vcc_veritas

# Configure environment
export THEMIS_URL="http://127.0.0.1:8765"
export LOG_LEVEL="INFO"

# Run adapter
uvicorn app:app --host 127.0.0.1 --port 8003 --reload
```

## API Endpoints

### Health Check

```bash
curl http://localhost:8003/health
```

### Verify Document

```bash
curl -X POST http://localhost:8003/verify/document \
  -F "file=@document.txt" \
  -F "data_classification=confidential" \
  -F "compliance_level=high" \
  -F "verified_by=john.doe"
```

Response:
```json
{
  "status": "verified",
  "checksum": "a3f5c8d9e2b1...",
  "import_result": { ... }
}
```

### Verify Compliance

```bash
curl -X POST http://localhost:8003/verify/compliance \
  -H "Content-Type: application/json" \
  -d '{
    "data": {
      "contract_id": "C-2024-001",
      "party_a": "Company X",
      "party_b": "Company Y",
      "amount": 50000
    },
    "verification": {
      "verification_status": "verified",
      "compliance_level": "high",
      "data_classification": "confidential",
      "verified_by": "compliance.officer"
    },
    "tags": ["contract", "legal"]
  }'
```

### Record Audit Entry

```bash
curl -X POST http://localhost:8003/audit/record \
  -H "Content-Type: application/json" \
  -d '{
    "action": "data_access",
    "entity_key": "users:alice",
    "user": "admin",
    "details": {
      "ip_address": "192.168.1.100",
      "timestamp": "2024-11-22T10:30:00Z"
    }
  }'
```

### Validate Data Integrity

```bash
curl -X POST http://localhost:8003/validate/integrity \
  -H "Content-Type: application/json" \
  -d '"users:alice"'
```

Response:
```json
{
  "status": "checked",
  "entity_key": "users:alice",
  "checksum_present": true,
  "checksum_format_valid": true,
  "has_verification_metadata": true,
  "stored_checksum": "abc123...",
  "note": "Full integrity validation requires original data. Use ThemisDB audit features for comprehensive verification."
}
```

**Note:** This endpoint performs basic checksum verification. For comprehensive integrity validation requiring data reconstruction, use ThemisDB's built-in audit and compliance features.

### Classify Data

```bash
# Manual classification
curl -X POST http://localhost:8003/classify/data \
  -F "file=@report.txt" \
  -F "classification=confidential" \
  -F "auto_classify=false"

# Auto-classification
curl -X POST http://localhost:8003/classify/data \
  -F "file=@report.txt" \
  -F "classification=internal" \
  -F "auto_classify=true"
```

## Data Classification Levels

VCC-Veritas supports standard data classification levels:

1. **Public** - Information that can be freely shared
2. **Internal** - Information for internal use only
3. **Confidential** - Sensitive information requiring protection
4. **Restricted** - Highly sensitive information with strict access controls

### Auto-Classification

Auto-classification uses keyword detection with scoring to reduce false positives:

**High Confidence Indicators:**
- "classified", "restricted" → **Confidential**
- Multiple occurrences of "confidential", "secret" → **Confidential**
- "internal only", "employee only", "staff only" → **Internal**
- "public", "press release", "published" → **Public**

**Important:** Auto-classification is a heuristic aid and may produce false positives in contexts where sensitive keywords appear in non-sensitive contexts (e.g., discussing classification policies). Always use manual verification for truly sensitive data, especially for "confidential" or "restricted" classifications.

Default classification when auto-classification is inconclusive: **Internal** (safe default)

## Verification Metadata

All verified data includes:

- `verification_status` - Current verification status
- `compliance_level` - Compliance level (high, medium, low)
- `data_classification` - Data classification level
- `verification_method` - Method used for verification
- `checksum` - SHA-256 checksum for integrity
- `verified_by` - User or system that performed verification
- `verification_date` - Timestamp of verification

## Configuration

### Environment Variables

- `THEMIS_URL` - ThemisDB base URL (default: http://127.0.0.1:8765)
- `THEMIS_AUTH_TOKEN` - Optional JWT authentication token
- `LOG_LEVEL` - Logging level: DEBUG, INFO, WARNING, ERROR (default: INFO)

## Use Cases

### 1. Contract Verification

```python
import httpx
import asyncio

async def verify_contract(contract_data):
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "http://localhost:8003/verify/compliance",
            json={
                "data": contract_data,
                "verification": {
                    "compliance_level": "high",
                    "data_classification": "confidential",
                    "verified_by": "legal.team"
                },
                "tags": ["contract", "legal", "verified"]
            }
        )
        return response.json()

contract = {
    "contract_id": "C-2024-001",
    "parties": ["Company A", "Company B"],
    "value": 100000
}

result = asyncio.run(verify_contract(contract))
print(f"Contract verified: {result['checksum']}")
```

### 2. Audit Trail for GDPR Compliance

```python
async def log_data_access(user_id, accessed_by, reason):
    async with httpx.AsyncClient() as client:
        response = await client.post(
            "http://localhost:8003/audit/record",
            json={
                "action": "personal_data_access",
                "entity_key": f"users:{user_id}",
                "user": accessed_by,
                "details": {
                    "reason": reason,
                    "gdpr_compliant": True
                }
            }
        )
        return response.json()

# Log GDPR data access
await log_data_access(
    user_id="alice",
    accessed_by="admin",
    reason="Customer support request"
)
```

### 3. Document Classification Pipeline

```python
async def classify_and_verify(filepath, auto=True):
    async with httpx.AsyncClient() as client:
        with open(filepath, "rb") as f:
            response = await client.post(
                "http://localhost:8003/classify/data",
                files={"file": f},
                data={
                    "classification": "internal",
                    "auto_classify": str(auto).lower()
                }
            )
        return response.json()

# Auto-classify documents
for doc in ["report1.txt", "report2.txt", "report3.txt"]:
    result = await classify_and_verify(doc, auto=True)
    print(f"{doc}: {result['classification']}")
```

## Architecture

```
┌─────────────────────────────┐
│   VCC-Veritas Frontend      │
│   (Compliance Dashboard)    │
└─────────────┬───────────────┘
              │ HTTP POST
              ▼
┌─────────────────────────────┐
│   VCC-Veritas Adapter       │
│   (FastAPI, Port 8003)      │
│  ┌───────────────────────┐  │
│  │ Document Verification │  │
│  │ Compliance Checking   │  │
│  │ Audit Recording       │  │
│  │ Integrity Validation  │  │
│  │ Data Classification   │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ Uses vcc_base
              ▼
┌─────────────────────────────┐
│   VCC Base Library          │
│  ┌───────────────────────┐  │
│  │ ThemisDB Client       │  │
│  │ Configuration         │  │
│  └───────────────────────┘  │
└─────────────┬───────────────┘
              │ HTTP (no UDS3)
              ▼
┌─────────────────────────────┐
│   ThemisDB Backend          │
│   (Port 8765)               │
└─────────────────────────────┘
```

## Security Considerations

1. **Checksum Integrity** - All verified data includes SHA-256 checksums
2. **Audit Trail** - All operations are logged for compliance
3. **Data Classification** - Automatic classification helps prevent data leaks
4. **Access Control** - Use `THEMIS_AUTH_TOKEN` for authenticated access
5. **Namespace Isolation** - Use separate namespaces for different security levels

## Monitoring

The adapter logs all verification and compliance operations:

```
2024-11-22 10:30:45 - vcc_veritas - INFO - Verified and imported document: contract.pdf, checksum=a3f5c8d9e2b1...
2024-11-22 10:30:46 - vcc_veritas - INFO - Compliance verification completed, checksum=f7e2c1d9a8b3...
2024-11-22 10:30:47 - vcc_veritas - INFO - Audit entry recorded: action=data_access, checksum=c9d2e8f1a4b7...
```

Set `LOG_LEVEL=DEBUG` for detailed debugging information.

## Troubleshooting

**Connection Error to ThemisDB:**
```
Failed to connect to ThemisDB at http://127.0.0.1:8765
```
→ Ensure ThemisDB is running: `./themis_server --port 8765`

**Checksum Mismatch:**
```
is_valid: false, stored_checksum != calculated_checksum
```
→ Data may have been modified or corrupted. Investigate audit logs.

**Module not found: vcc_base:**
```
ModuleNotFoundError: No module named 'vcc_base'
```
→ Install vcc_base dependencies: `cd ../vcc_base && pip install -r requirements.txt`

## Related Documentation

- [VCC Base Library](../vcc_base/README.md) - Shared adapter library
- [ThemisDB Audit API](../../docs/development/audit_api_implementation.md) - Audit capabilities
- [ThemisDB Security](../../docs/security/) - Security features

## License

See main ThemisDB project license.
