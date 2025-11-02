#!/bin/bash
# Restore Script for ThemisDB (Bash version)
# Restores database from checkpoint with optional WAL replay
#
# Usage:
#   ./restore.sh --checkpoint-dir /backup/themis/checkpoints/checkpoint-20251102-120000 --db-path /var/lib/themis
#   ./restore.sh --checkpoint-dir /backup/themis/checkpoints/checkpoint-20251102-120000 --db-path /var/lib/themis --wal-archive-dir /backup/themis/wal-archive --force

set -euo pipefail

CHECKPOINT_DIR=""
DB_PATH=""
WAL_ARCHIVE_DIR=""
FORCE=0
VERIFY=0

function log() {
    local level="${1:-INFO}"
    local message="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo "[$timestamp] [$level] $message"
}

function usage() {
    cat <<EOF
Usage: $0 --checkpoint-dir PATH --db-path PATH [OPTIONS]

Required:
  --checkpoint-dir PATH   Path to checkpoint directory to restore from
  --db-path PATH          Target database path (will be overwritten)

Optional:
  --wal-archive-dir PATH  WAL archive directory for point-in-time recovery
  --force                 Overwrite existing database without prompting
  --verify                Verify restoration by checking file structure
  --help                  Show this help message

Example:
  $0 --checkpoint-dir /backup/themis/checkpoints/checkpoint-20251102-120000 \\
     --db-path /var/lib/themis --force --verify
EOF
    exit 0
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --checkpoint-dir)
            CHECKPOINT_DIR="$2"
            shift 2
            ;;
        --db-path)
            DB_PATH="$2"
            shift 2
            ;;
        --wal-archive-dir)
            WAL_ARCHIVE_DIR="$2"
            shift 2
            ;;
        --force)
            FORCE=1
            shift
            ;;
        --verify)
            VERIFY=1
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
if [[ -z "$CHECKPOINT_DIR" ]] || [[ -z "$DB_PATH" ]]; then
    echo "Error: --checkpoint-dir and --db-path are required"
    usage
fi

log INFO "=== ThemisDB Restore Started ==="
log INFO "Checkpoint: $CHECKPOINT_DIR"
log INFO "Target DB Path: $DB_PATH"

# Validate checkpoint exists
if [[ ! -d "$CHECKPOINT_DIR" ]]; then
    log ERROR "Checkpoint directory does not exist: $CHECKPOINT_DIR"
    exit 1
fi

# Check for manifest
MANIFEST_PATH="$CHECKPOINT_DIR/manifest.json"
if [[ -f "$MANIFEST_PATH" ]]; then
    log INFO "Manifest found:"
    cat "$MANIFEST_PATH" | python3 -m json.tool 2>/dev/null || cat "$MANIFEST_PATH"
else
    log WARN "No manifest found (manual checkpoint or old format)"
fi

# Safety check: DB path exists
if [[ -d "$DB_PATH" ]]; then
    if [[ $FORCE -eq 0 ]]; then
        log ERROR "Target DB path already exists: $DB_PATH"
        log ERROR "Use --force to overwrite"
        exit 1
    fi
    log WARN "Removing existing DB (Force mode)..."
    rm -rf "$DB_PATH"
fi

# Restore checkpoint
log INFO "Restoring checkpoint..."
cp -r "$CHECKPOINT_DIR" "$DB_PATH"
# Remove manifest from restored DB (it's metadata, not DB data)
rm -f "$DB_PATH/manifest.json" 2>/dev/null || true
log INFO "Checkpoint restored to $DB_PATH"

# Optional: Replay WAL files
if [[ -n "$WAL_ARCHIVE_DIR" ]] && [[ -d "$WAL_ARCHIVE_DIR" ]]; then
    log INFO "Replaying WAL files from $WAL_ARCHIVE_DIR..."
    WAL_DEST_DIR="$DB_PATH/.rocksdb"
    mkdir -p "$WAL_DEST_DIR"
    
    REPLAYED_COUNT=0
    for wal in "$WAL_ARCHIVE_DIR"/*.log; do
        [[ -f "$wal" ]] || continue
        WAL_NAME=$(basename "$wal" | sed 's/^[0-9]\{8\}-[0-9]\{6\}-//')
        DEST_WAL="$WAL_DEST_DIR/$WAL_NAME"
        cp "$wal" "$DEST_WAL"
        ((REPLAYED_COUNT++))
    done
    log INFO "Replayed $REPLAYED_COUNT WAL files"
    log WARN "Note: Manual WAL replay may require RocksDB repair. Use with caution."
fi

# Verify restoration
if [[ $VERIFY -eq 1 ]]; then
    log INFO "Verifying restored database..."
    
    # Check basic file structure
    FILES_OK=1
    for pattern in "CURRENT" "MANIFEST-*" "OPTIONS-*"; do
        if ! ls "$DB_PATH"/$pattern >/dev/null 2>&1; then
            log WARN "Verification: Missing expected file pattern: $pattern"
            FILES_OK=0
        fi
    done
    
    if [[ $FILES_OK -eq 1 ]]; then
        log INFO "Verification: Basic file structure looks good ✓"
    else
        log WARN "Verification: Some expected files missing. DB may need repair."
    fi
    
    # Try API health check
    if curl -f -s http://localhost:8765/health >/dev/null 2>&1; then
        log INFO "Verification: Server health check passed ✓"
    else
        log WARN "Verification: Could not reach server (may need manual start)"
    fi
fi

log INFO "=== Restore Completed Successfully ==="
log INFO "Restored DB at: $DB_PATH"
log INFO "Next step: Start ThemisDB server and verify data integrity"

exit 0
