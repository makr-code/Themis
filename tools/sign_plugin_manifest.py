#!/usr/bin/env python3
"""
Plugin Manifest Signature Generator

Generates SHA256 signature files for plugin.json manifests.

Usage:
    python tools/sign_plugin_manifest.py plugin.json

This creates plugin.json.sig with the SHA256 hash.
"""

import sys
import hashlib
import os

def compute_sha256(file_path):
    """Compute SHA256 hash of a file"""
    sha256_hash = hashlib.sha256()
    with open(file_path, "rb") as f:
        for byte_block in iter(lambda: f.read(4096), b""):
            sha256_hash.update(byte_block)
    return sha256_hash.hexdigest()

def sign_manifest(manifest_path):
    """Generate signature file for a manifest"""
    if not os.path.exists(manifest_path):
        print(f"Error: Manifest not found: {manifest_path}", file=sys.stderr)
        return False
    
    # Compute hash
    hash_value = compute_sha256(manifest_path)
    
    # Write signature file
    sig_path = manifest_path + ".sig"
    with open(sig_path, "w") as f:
        f.write(hash_value + "\n")
    
    print(f"✓ Generated signature for {manifest_path}")
    print(f"  SHA256: {hash_value}")
    print(f"  Signature file: {sig_path}")
    
    return True

def main():
    if len(sys.argv) < 2:
        print("Usage: python sign_plugin_manifest.py <plugin.json>", file=sys.stderr)
        print("", file=sys.stderr)
        print("Examples:", file=sys.stderr)
        print("  python tools/sign_plugin_manifest.py plugins/blob/filesystem/plugin.json", file=sys.stderr)
        print("  python tools/sign_plugin_manifest.py plugins/importers/postgres/plugin.json", file=sys.stderr)
        return 1
    
    manifest_path = sys.argv[1]
    
    if not sign_manifest(manifest_path):
        return 1
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
