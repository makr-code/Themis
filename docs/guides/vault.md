weiter ```markdown
# Vault integration (developer notes)

This document explains how to run the Vault developer helper and the repository's Vault integration tests locally.

Prerequisites
- Docker (local, able to run Linux containers)
- WSL (Windows Subsystem for Linux) if you intend to run the test binary from WSL
- A repository build producing `build-wsl/themis_tests` or `build-wsl/themis_server`

Helper script
- `.tools/vault_dev_run.ps1` is a PowerShell helper that:
  - starts a Vault dev container (`hashicorp/vault`), exposing it on `127.0.0.1:8200`
  - enables KV v2 at the mount `themis/`
  - writes a test secret to `themis/keys/test_key` with a 32‑byte random payload encoded in base64 (suitable for AES‑256 key material)
  - runs the VaultKeyProvider tests via WSL and writes test output (JUnit XML and console log) to `C:\Temp`

Usage (PowerShell)

```powershell
# From repository root on Windows PowerShell
.\.tools\vault_dev_run.ps1

# To only start/seed Vault but not run tests:
.\.tools\vault_dev_run.ps1 -NoRunTests
```

How it works (important details)
- The script reads the Vault root token from the Vault container logs and sets `VAULT_ADDR`/`VAULT_TOKEN` for subsequent commands.
- It writes the key using `vault kv put themis/keys/test_key key=<base64>` and sets metadata fields like `algorithm="AES-256-GCM"` and `version=1` — the tests expect the secret to be present under that path.
- The key material must be exactly 32 bytes before base64 encoding (the helper uses `/dev/urandom` and `head -c 32 | base64`). If you create the secret manually ensure the raw key is 32 bytes.

Running tests manually (WSL)

1. Ensure Vault dev is running and the key exists (the helper does this for you).
2. From PowerShell run the WSL command shown by the helper or run manually in WSL:

```bash
cd /mnt/c/VCC/themis
export VAULT_ADDR='http://127.0.0.1:8200'
export VAULT_TOKEN='<root-token-from-helper-or-logs>'
./build-wsl/themis_tests --gtest_filter=VaultKeyProviderTest.* --gtest_output=xml:/mnt/c/Temp/themis_vault_tests.xml | tee /mnt/c/Temp/themis_vault_tests.txt
```

Troubleshooting
- If tests fail with 401/permission or "secret not found", confirm:
  - `VAULT_ADDR` and `VAULT_TOKEN` are exported in the same shell executing the test binary
  - The secret is present at `themis/keys/test_key` (use `vault kv get themis/keys/test_key` inside the container)
  - The secret value was written as a raw 32‑byte key encoded to base64 (the VaultKeyProvider expects 32‑byte AES key material)

Notes
- This helper and these instructions are intended for local developer testing only. In CI, provide a stable Vault instance or mock the key provider to avoid network dependencies.

```
