#!/usr/bin/env pwsh
# Incremental Backup Script for ThemisDB
# Creates RocksDB checkpoints and archives WAL files for point-in-time recovery
#
# Usage:
#   .\backup-incremental.ps1 -DbPath "C:\data\themis" -BackupRoot "C:\backups\themis"
#   .\backup-incremental.ps1 -DbPath "/var/lib/themis" -BackupRoot "/backup/themis" (PowerShell Core on Linux)

param(
    [Parameter(Mandatory=$true)]
    [string]$DbPath,
    
    [Parameter(Mandatory=$true)]
    [string]$BackupRoot,
    
    [int]$RetentionDays = 7,
    
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

function Write-Log {
    param([string]$Message, [string]$Level = "INFO")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMsg = "[$timestamp] [$Level] $Message"
    Write-Host $logMsg
    Add-Content -Path (Join-Path $BackupRoot "backup.log") -Value $logMsg -ErrorAction SilentlyContinue
}

# Validate DB path exists
if (-not (Test-Path $DbPath)) {
    Write-Log "DB path does not exist: $DbPath" "ERROR"
    exit 1
}

# Create backup root if missing
if (-not (Test-Path $BackupRoot)) {
    New-Item -ItemType Directory -Path $BackupRoot -Force | Out-Null
    Write-Log "Created backup root: $BackupRoot"
}

# Timestamp for this backup
$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$checkpointDir = Join-Path $BackupRoot "checkpoints\checkpoint-$timestamp"
$walArchiveDir = Join-Path $BackupRoot "wal-archive"

Write-Log "=== ThemisDB Incremental Backup Started ==="
Write-Log "DB Path: $DbPath"
Write-Log "Backup Root: $BackupRoot"
Write-Log "Timestamp: $timestamp"

# Step 1: Create checkpoint via HTTP API (assumes server running on localhost:8765)
# Alternative: Call rocksdb_wrapper createCheckpoint directly if using library mode
Write-Log "Creating checkpoint..."

try {
    # Ensure checkpoint parent exists
    $checkpointParent = Split-Path $checkpointDir -Parent
    if (-not (Test-Path $checkpointParent)) {
        New-Item -ItemType Directory -Path $checkpointParent -Force | Out-Null
    }

    # Option A: HTTP API (if server is running)
    $apiUrl = "http://localhost:8765/admin/backup"
    $body = @{ checkpoint_dir = $checkpointDir } | ConvertTo-Json
    try {
        $response = Invoke-RestMethod -Uri $apiUrl -Method Post -Body $body -ContentType "application/json" -ErrorAction Stop
        if ($response.success -eq $true) {
            Write-Log "Checkpoint created via API: $checkpointDir"
        } else {
            Write-Log "API checkpoint failed: $($response.message)" "WARN"
            # Fallback: manual copy (not ideal for consistency)
            throw "API failed, attempting manual copy"
        }
    } catch {
        Write-Log "API not available, falling back to filesystem copy" "WARN"
        # Option B: Manual copy (requires DB to be offline or using external tools)
        # For production, use RocksDB Backup API or ensure server handles checkpoint
        Copy-Item -Path $DbPath -Destination $checkpointDir -Recurse -Force
        Write-Log "Manual checkpoint copy completed: $checkpointDir"
    }
} catch {
    Write-Log "Checkpoint creation failed: $_" "ERROR"
    exit 1
}

# Step 2: Archive WAL files
Write-Log "Archiving WAL files..."
$walSourceDir = Join-Path $DbPath ".rocksdb"
if (Test-Path $walSourceDir) {
    if (-not (Test-Path $walArchiveDir)) {
        New-Item -ItemType Directory -Path $walArchiveDir -Force | Out-Null
    }
    
    $walFiles = Get-ChildItem -Path $walSourceDir -Filter "*.log" -File
    $archivedCount = 0
    foreach ($wal in $walFiles) {
        $destFile = Join-Path $walArchiveDir "$timestamp-$($wal.Name)"
        if (-not (Test-Path $destFile)) {
            Copy-Item -Path $wal.FullName -Destination $destFile -Force
            $archivedCount++
            if ($Verbose) {
                Write-Log "Archived WAL: $($wal.Name)" "DEBUG"
            }
        }
    }
    Write-Log "Archived $archivedCount WAL files to $walArchiveDir"
} else {
    Write-Log "WAL directory not found: $walSourceDir" "WARN"
}

# Step 3: Cleanup old backups (retention policy)
Write-Log "Applying retention policy (keep last $RetentionDays days)..."
$checkpointsRoot = Join-Path $BackupRoot "checkpoints"
if (Test-Path $checkpointsRoot) {
    $cutoffDate = (Get-Date).AddDays(-$RetentionDays)
    $oldCheckpoints = Get-ChildItem -Path $checkpointsRoot -Directory | Where-Object {
        $_.CreationTime -lt $cutoffDate
    }
    foreach ($old in $oldCheckpoints) {
        Remove-Item -Path $old.FullName -Recurse -Force
        Write-Log "Removed old checkpoint: $($old.Name)"
    }
}

# Cleanup old WAL archives (keep same retention)
if (Test-Path $walArchiveDir) {
    $cutoffDate = (Get-Date).AddDays(-$RetentionDays)
    $oldWals = Get-ChildItem -Path $walArchiveDir -File | Where-Object {
        $_.CreationTime -lt $cutoffDate
    }
    foreach ($old in $oldWals) {
        Remove-Item -Path $old.FullName -Force
        if ($Verbose) {
            Write-Log "Removed old WAL: $($old.Name)" "DEBUG"
        }
    }
    Write-Log "Cleaned up old WAL files (older than $RetentionDays days)"
}

# Step 4: Generate backup manifest
$manifest = @{
    timestamp = $timestamp
    db_path = $DbPath
    checkpoint_dir = $checkpointDir
    wal_archive_dir = $walArchiveDir
    retention_days = $RetentionDays
    backup_size_mb = [math]::Round((Get-ChildItem -Path $checkpointDir -Recurse | Measure-Object -Property Length -Sum).Sum / 1MB, 2)
} | ConvertTo-Json

$manifestPath = Join-Path $checkpointDir "manifest.json"
Set-Content -Path $manifestPath -Value $manifest
Write-Log "Backup manifest: $manifestPath"

Write-Log "=== Incremental Backup Completed Successfully ==="
Write-Log "Checkpoint: $checkpointDir"
Write-Log "Size: $($manifest | ConvertFrom-Json | Select-Object -ExpandProperty backup_size_mb) MB"

exit 0
