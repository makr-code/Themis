#!/usr/bin/env bash
set -euo pipefail

CONFIG_TEMPLATE="/usr/local/share/themis/config.qnap.json"
TARGET_CONFIG="${THEMIS_CONFIG_PATH:-/etc/vccdb/config.json}"
PORT="${THEMIS_PORT:-18765}"

# Ensure persistent directories exist with permissive ownership
mkdir -p /data /data/themis_server /data/vector_indexes || true
chmod 777 /data /data/themis_server /data/vector_indexes || true

# If no config is mounted, seed from template
if [ ! -f "$TARGET_CONFIG" ]; then
  echo "[entrypoint] No config at $TARGET_CONFIG, seeding from template"
  mkdir -p "$(dirname "$TARGET_CONFIG")"
  cp "$CONFIG_TEMPLATE" "$TARGET_CONFIG"
fi

# Rewrite JSON config to reflect chosen port and data paths
# Requires jq (present in runtime image)
TMP_CFG="${TARGET_CONFIG}.tmp"
jq \
  --argjson port "$PORT" \
  '.server.port = ($port|tonumber)
   | .storage.rocksdb_path = "/data/themis_server"
   | .vector_index.save_path = "/data/vector_indexes"' \
  "$TARGET_CONFIG" > "$TMP_CFG"

mv -f "$TMP_CFG" "$TARGET_CONFIG"

echo "[entrypoint] Using config: $TARGET_CONFIG"
cat "$TARGET_CONFIG" | jq '.server, .storage.rocksdb_path, .vector_index.save_path' || true

exec /usr/local/bin/themis_server --config "$TARGET_CONFIG"
