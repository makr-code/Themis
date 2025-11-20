#!/usr/bin/env python3
"""Initialize security signature for config/mime_types.yaml

Computes SHA256 hash of the YAML file and stores it in ThemisDB via HTTP API.

Usage:
  python scripts/init_mime_signature.py [--server http://localhost:8080] [--resource config/mime_types.yaml]
"""
import sys
import os
import hashlib
import requests
import argparse
from datetime import datetime, timezone
import json

def compute_file_hash(file_path: str) -> str:
    """Compute SHA256 hash of file"""
    with open(file_path, 'rb') as f:
        content = f.read()
    return hashlib.sha256(content).hexdigest()

def store_signature(server_url: str, resource_id: str, file_hash: str, comment: str = "") -> bool:
    """Store signature via HTTP API"""
    url = f"{server_url}/api/security/signatures"
    
    payload = {
        "resource_id": resource_id,
        "hash": file_hash,
        "algorithm": "sha256",
        "created_at": int(datetime.now(timezone.utc).timestamp()),
        "created_by": "init_script",
        "comment": comment or f"Signature for {resource_id}"
    }
    
    try:
        response = requests.post(url, json=payload, timeout=10)
        response.raise_for_status()
        result = response.json()
        print(f"✓ Signature stored successfully")
        print(f"  Resource ID: {result.get('resource_id')}")
        print(f"  Created at: {result.get('created_at')}")
        return True
    except requests.exceptions.RequestException as e:
        print(f"✗ Failed to store signature: {e}", file=sys.stderr)
        if hasattr(e, 'response') and e.response is not None:
            try:
                error_detail = e.response.json()
                print(f"  Server error: {error_detail}", file=sys.stderr)
            except:
                print(f"  Response: {e.response.text}", file=sys.stderr)
        return False

def verify_signature(server_url: str, resource_id: str, file_path: str) -> bool:
    """Verify signature via HTTP API"""
    # URL encode resource_id for path parameter
    import urllib.parse
    encoded_resource = urllib.parse.quote(resource_id, safe='')
    url = f"{server_url}/api/security/verify/{encoded_resource}"
    
    payload = {"file_path": file_path}
    
    try:
        response = requests.post(url, json=payload, timeout=10)
        result = response.json()
        
        if result.get('verified'):
            print(f"✓ Verification successful")
            print(f"  Current hash: {result.get('current_hash')}")
            print(f"  Stored hash:  {result.get('stored_hash')}")
            return True
        else:
            print(f"✗ Verification FAILED", file=sys.stderr)
            print(f"  Current hash: {result.get('current_hash')}", file=sys.stderr)
            print(f"  Stored hash:  {result.get('stored_hash')}", file=sys.stderr)
            return False
    except requests.exceptions.RequestException as e:
        print(f"✗ Verification request failed: {e}", file=sys.stderr)
        return False

def main():
    parser = argparse.ArgumentParser(description='Initialize security signature for MIME config')
    parser.add_argument('--server', default='http://localhost:8080', help='ThemisDB HTTP server URL')
    parser.add_argument('--resource', default='config/mime_types.yaml', help='Resource ID (file path)')
    parser.add_argument('--file', help='Actual file path (if different from resource ID)')
    parser.add_argument('--comment', default='', help='Optional comment for signature')
    parser.add_argument('--verify-only', action='store_true', help='Only verify, do not store')
    
    args = parser.parse_args()
    
    file_path = args.file or args.resource
    
    if not os.path.exists(file_path):
        print(f"Error: File not found: {file_path}", file=sys.stderr)
        sys.exit(1)
    
    print(f"File: {file_path}")
    print(f"Resource ID: {args.resource}")
    print(f"Server: {args.server}")
    print()
    
    # Compute hash
    print("Computing SHA256 hash...")
    file_hash = compute_file_hash(file_path)
    print(f"Hash: {file_hash}")
    print()
    
    if args.verify_only:
        # Verify only mode
        success = verify_signature(args.server, args.resource, file_path)
        sys.exit(0 if success else 1)
    else:
        # Store and verify
        print("Storing signature...")
        if not store_signature(args.server, args.resource, file_hash, args.comment):
            sys.exit(1)
        print()
        
        print("Verifying signature...")
        if not verify_signature(args.server, args.resource, file_path):
            print("\nWarning: Verification failed after storing! Check server logs.", file=sys.stderr)
            sys.exit(1)
        
        print("\n✓ Signature initialized successfully")
        sys.exit(0)

if __name__ == '__main__':
    main()
