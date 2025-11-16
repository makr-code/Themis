<#
Vault Dev helper script for Windows PowerShell

Usage:
  - Open PowerShell as user that can run Docker and WSL.
  - Run: `./.tools/vault_dev_run.ps1` to start Vault dev, configure KV v2, write a test key,
    run the VaultKeyProviderTest.* tests via WSL and then stop the container.

Notes:
  - This script expects the repo to be at `C:\VCC\themis` and WSL build to be in `build-wsl`.
  - Test outputs (console + xml) are written to `C:\Temp`.
#>

param(
    [switch]$NoRunTests = $false,
    [string]$TestFilter = "VaultKeyProviderTest.*"
)

function Write-Log { param($m) Write-Host "[vault-dev] $m" }

Write-Log "Starting Vault dev container..."
# Start Vault dev (detached)
$existing = docker ps -a --filter "name=themis-vault" --format "{{.ID}}" 2>$null
if ($existing) {
    Write-Log "Container 'themis-vault' already exists. Removing old instance..."
    docker rm -f themis-vault | Out-Null
}

$runId = docker run -d --name themis-vault -p 8200:8200 hashicorp/vault:1.14.0 server -dev -dev-root-token-id=s 2>&1
if (-not $runId) { Write-Log "Failed to start container"; exit 1 }
Write-Log "Started container: $runId"

# Wait for Vault to be ready by polling logs for 'Root Token' line
Write-Log "Waiting for Vault to initialize (root token)..."
$token = $null
for ($i=0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 1
    $logs = docker logs themis-vault --tail 200 2>$null
    if ($logs -match "Root Token:\s*(\S+)") {
        $token = $Matches[1]
        break
    }
}
if (-not $token) { Write-Log "Failed to detect Vault root token in logs"; docker logs themis-vault --tail 200; exit 2 }
Write-Log "Detected Vault root token: $token"

# Enable KV v2 at 'themis' and write a test key
Write-Log "Enabling KV v2 at mount path 'themis'..."
$env:VAULT_ADDR = "http://127.0.0.1:8200"
$env:VAULT_TOKEN = $token
# Use docker exec to run the vault CLI inside container
docker exec -e VAULT_ADDR=http://127.0.0.1:8200 -e VAULT_TOKEN=$token themis-vault vault secrets enable -version=2 -path=themis kv
if ($LASTEXITCODE -ne 0) { Write-Log "Warning: enabling kv v2 may have failed (it might already be enabled). Continuing..." }

Write-Log "Writing test key to themis/keys/test_key..."
# Use sh -c to run head/base64 inside container
docker exec -e VAULT_ADDR=http://127.0.0.1:8200 -e VAULT_TOKEN=$token themis-vault sh -c 'vault kv put themis/keys/test_key key=$(head -c 32 /dev/urandom | base64) algorithm="AES-256-GCM" version=1'

if ($NoRunTests) {
    Write-Log "Skipping running tests as requested (NoRunTests=true)"
} else {
    Write-Log "Running VaultKeyProvider tests inside WSL..."
    $xmlOut = "/mnt/c/Temp/themis_vault_tests.xml"
    $txtOut = "/mnt/c/Temp/themis_vault_tests.txt"
    $wslCmd = "cd /mnt/c/VCC/themis; export VAULT_ADDR='http://127.0.0.1:8200'; export VAULT_TOKEN='$token'; ./build-wsl/themis_tests --gtest_filter=$TestFilter --gtest_output=xml:$xmlOut | tee $txtOut"
    wsl bash -lc $wslCmd
}

Write-Log "Stopping and removing Vault container..."
docker rm -f themis-vault | Out-Null
Write-Log "Done. Logs + XML (if tests ran) are in C:\\Temp (check themis_vault_tests.*)."
