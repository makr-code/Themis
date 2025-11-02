#!/usr/bin/env pwsh
# Restore Script for ThemisDB
# Restores database from checkpoint with optional WAL replay
#
# Usage:
#   .\restore.ps1 -CheckpointDir "C:\backups\themis\checkpoints\checkpoint-20251102-120000" -DbPath "C:\data\themis"
#   .\restore.ps1 -CheckpointDir "/backup/themis/checkpoints/checkpoint-20251102-120000" -DbPath "/var/lib/themis"

param(
    [Parameter(Mandatory=$true)]
    [string]$CheckpointDir,
    
    [Parameter(Mandatory=$true)]
    [string]$DbPath,
    
    [string]$WalArchiveDir = "",
    
    [switch]$Force,
    
    [switch]$Verify
)

$ErrorActionPreference = "Stop"

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    Write-Host "[$timestamp] [$Level] $Message"
}

Write-Log "=== ThemisDB Restore Started ==="
Write-Log "Checkpoint: $CheckpointDir"
Write-Log "Target DB Path: $DbPath"

# Validate checkpoint exists
if (-not (Test-Path $CheckpointDir)) {
    Write-Log "Checkpoint directory does not exist: $CheckpointDir" "ERROR"
    exit 1
}

# Check for manifest
$manifestPath = Join-Path $CheckpointDir "manifest.json"
if (Test-Path $manifestPath) {
    $manifest = Get-Content $manifestPath | ConvertFrom-Json
    Write-Log "Manifest found: backup from $($manifest.timestamp)"
    Write-Log "Original DB: $($manifest.db_path)"
    Write-Log "Backup size: $($manifest.backup_size_mb) MB"
} else {
    Write-Log "No manifest found (manual checkpoint or old format)" "WARN"
}

# Safety check: DB path exists
if (Test-Path $DbPath) {
    if (-not $Force) {
        Write-Log "Target DB path already exists: $DbPath" "ERROR"
        Write-Log "Use -Force to overwrite" "ERROR"
        exit 1
    }
    Write-Log "Removing existing DB (Force mode)..." "WARN"
    Remove-Item -Path $DbPath -Recurse -Force
}

# Restore checkpoint
Write-Log "Restoring checkpoint..."
Copy-Item -Path $CheckpointDir -Destination $DbPath -Recurse -Force
Write-Log "Checkpoint restored to $DbPath"

# Optional: Replay WAL files (if provided)
if ($WalArchiveDir -and (Test-Path $WalArchiveDir)) {
    Write-Log "Replaying WAL files from $WalArchiveDir..."
    $walDestDir = Join-Path $DbPath ".rocksdb"
    if (-not (Test-Path $walDestDir)) {
        New-Item -ItemType Directory -Path $walDestDir -Force | Out-Null
    }
    
    $walFiles = Get-ChildItem -Path $WalArchiveDir -Filter "*.log" -File | Sort-Object Name
    $replayedCount = 0
    foreach ($wal in $walFiles) {
        # Extract timestamp prefix from archived WAL (format: YYYYMMDD-HHMMSS-original.log)
        $walName = $wal.Name -replace '^\d{8}-\d{6}-', ''
        $destWal = Join-Path $walDestDir $walName
        Copy-Item -Path $wal.FullName -Destination $destWal -Force
        $replayedCount++
    }
    Write-Log "Replayed $replayedCount WAL files"
    Write-Log "Note: Manual WAL replay may require RocksDB repair. Use with caution." "WARN"
}

# Verify restoration (optional)
if ($Verify) {
    Write-Log "Verifying restored database..."
    # Option A: Try opening via HTTP API (if server available)
    try {
        $apiUrl = "http://localhost:8765/health"
        $response = Invoke-RestMethod -Uri $apiUrl -Method Get -TimeoutSec 5 -ErrorAction Stop
        if ($response.status -eq "ok") {
            Write-Log "Verification: Server health check passed" "SUCCESS"
        }
    } catch {
        Write-Log "Verification: Could not reach server (may need manual start)" "WARN"
    }
    
    # Option B: Check basic file structure
    $requiredFiles = @("CURRENT", "MANIFEST-*", "OPTIONS-*")
    $filesOk = $true
    foreach ($pattern in $requiredFiles) {
        if (-not (Get-ChildItem -Path $DbPath -Filter $pattern -File)) {
            Write-Log "Verification: Missing expected file pattern: $pattern" "WARN"
            $filesOk = $false
        }
    }
    if ($filesOk) {
        Write-Log "Verification: Basic file structure looks good" "SUCCESS"
    }
}

Write-Log "=== Restore Completed Successfully ==="
Write-Log "Restored DB at: $DbPath"
Write-Log "Next step: Start ThemisDB server and verify data integrity"

exit 0
