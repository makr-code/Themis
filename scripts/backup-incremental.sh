#!/bin/bash
# Incremental Backup Script for ThemisDB (Bash version)
# Creates RocksDB checkpoints and archives WAL files
#
# Usage:
#   ./backup-incremental.sh --db-path /var/lib/themis --backup-root /backup/themis
#   ./backup-incremental.sh --db-path /var/lib/themis --backup-root /backup/themis --retention-days 14

set -euo pipefail

DB_PATH=""
BACKUP_ROOT=""
RETENTION_DAYS=7
VERBOSE=0

function log() {
    local level="${1:-INFO}"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] $message"
    echo "[$timestamp] [$level] $message" >> "$BACKUP_ROOT/backup.log" 2>/dev/null || true
}

function usage() {
    cat <<EOF
Usage: $0 --db-path PATH --backup-root PATH [OPTIONS]

Required:
  --db-path PATH          Path to RocksDB database directory
  --backup-root PATH      Root directory for backups

Optional:
  --retention-days DAYS   Keep backups for N days (default: 7)
  --verbose               Enable verbose logging
  --help                  Show this help message

Example:
  $0 --db-path /var/lib/themis --backup-root /backup/themis --retention-days 14
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --db-path)
            DB_PATH="$2"
            shift 2
            ;;
        --backup-root)
            BACKUP_ROOT="$2"
            shift 2
            ;;
        --retention-days)
            RETENTION_DAYS="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE=1
            shift
            ;;
        --help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

# Validate required params
if [[ -z "$DB_PATH" ]] || [[ -z "$BACKUP_ROOT" ]]; then
    echo "Error: --db-path and --backup-root are required"
    usage
fi

if [[ ! -d "$DB_PATH" ]]; then
    log ERROR "DB path does not exist: $DB_PATH"
    exit 1
fi

# Create backup root if missing
mkdir -p "$BACKUP_ROOT"
log INFO "=== ThemisDB Incremental Backup Started ==="
log INFO "DB Path: $DB_PATH"
log INFO "Backup Root: $BACKUP_ROOT"

# Timestamp for this backup
TIMESTAMP=$(date '+%Y%m%d-%H%M%S')
CHECKPOINT_DIR="$BACKUP_ROOT/checkpoints/checkpoint-$TIMESTAMP"
WAL_ARCHIVE_DIR="$BACKUP_ROOT/wal-archive"

log INFO "Timestamp: $TIMESTAMP"

# Step 1: Create checkpoint
log INFO "Creating checkpoint..."
mkdir -p "$(dirname "$CHECKPOINT_DIR")"

# Try API first (assumes server on localhost:8765)
API_URL="http://localhost:8765/admin/backup"
if curl -f -s -X POST "$API_URL" \
    -H "Content-Type: application/json" \
    -d "{\"checkpoint_dir\":\"$CHECKPOINT_DIR\"}" > /dev/null 2>&1; then
    log INFO "Checkpoint created via API: $CHECKPOINT_DIR"
else
    log WARN "API not available, using manual copy"
    # Fallback: manual copy (requires offline DB or external tools)
    cp -r "$DB_PATH" "$CHECKPOINT_DIR"
    log INFO "Manual checkpoint copy completed: $CHECKPOINT_DIR"
fi

# Step 2: Archive WAL files
log INFO "Archiving WAL files..."
WAL_SOURCE_DIR="$DB_PATH/.rocksdb"
if [[ -d "$WAL_SOURCE_DIR" ]]; then
    mkdir -p "$WAL_ARCHIVE_DIR"
    ARCHIVED_COUNT=0
    for wal in "$WAL_SOURCE_DIR"/*.log; do
        [[ -f "$wal" ]] || continue
        WAL_NAME=$(basename "$wal")
        DEST_FILE="$WAL_ARCHIVE_DIR/$TIMESTAMP-$WAL_NAME"
        if [[ ! -f "$DEST_FILE" ]]; then
            cp "$wal" "$DEST_FILE"
            ((ARCHIVED_COUNT++))
            [[ $VERBOSE -eq 1 ]] && log DEBUG "Archived WAL: $WAL_NAME"
        fi
    done
    log INFO "Archived $ARCHIVED_COUNT WAL files to $WAL_ARCHIVE_DIR"
else
    log WARN "WAL directory not found: $WAL_SOURCE_DIR"
fi

# Step 3: Cleanup old backups (retention policy)
log INFO "Applying retention policy (keep last $RETENTION_DAYS days)..."
CHECKPOINTS_ROOT="$BACKUP_ROOT/checkpoints"
if [[ -d "$CHECKPOINTS_ROOT" ]]; then
    find "$CHECKPOINTS_ROOT" -maxdepth 1 -type d -name "checkpoint-*" -mtime +$RETENTION_DAYS -exec rm -rf {} \; 2>/dev/null || true
    log INFO "Removed checkpoints older than $RETENTION_DAYS days"
fi

# Cleanup old WAL archives
if [[ -d "$WAL_ARCHIVE_DIR" ]]; then
    find "$WAL_ARCHIVE_DIR" -type f -name "*.log" -mtime +$RETENTION_DAYS -delete 2>/dev/null || true
    log INFO "Removed WAL files older than $RETENTION_DAYS days"
fi

# Step 4: Generate backup manifest
BACKUP_SIZE_MB=$(du -sm "$CHECKPOINT_DIR" | cut -f1)
cat > "$CHECKPOINT_DIR/manifest.json" <<EOF
{
  "timestamp": "$TIMESTAMP",
  "db_path": "$DB_PATH",
  "checkpoint_dir": "$CHECKPOINT_DIR",
  "wal_archive_dir": "$WAL_ARCHIVE_DIR",
  "retention_days": $RETENTION_DAYS,
  "backup_size_mb": $BACKUP_SIZE_MB
}
EOF

log INFO "Backup manifest: $CHECKPOINT_DIR/manifest.json"
log INFO "=== Incremental Backup Completed Successfully ==="
log INFO "Checkpoint: $CHECKPOINT_DIR"
log INFO "Size: ${BACKUP_SIZE_MB} MB"

exit 0
