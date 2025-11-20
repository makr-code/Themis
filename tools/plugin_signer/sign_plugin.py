#!/usr/bin/env python3
"""
ThemisDB Plugin Signer
Digitally signs plugin DLLs/SOs with RSA/ECDSA signatures

Usage:
    python sign_plugin.py <plugin_file> <private_key> <certificate> [--output metadata.json]
"""

import hashlib
import subprocess
import json
import base64
import sys
import os
from datetime import datetime
from pathlib import Path

def calculate_sha256(file_path):
    """Calculate SHA-256 hash of a file"""
    sha256 = hashlib.sha256()
    with open(file_path, 'rb') as f:
        while True:
            data = f.read(65536)  # 64KB chunks
            if not data:
                break
            sha256.update(data)
    return sha256.hexdigest()

def sign_file(file_path, private_key_path):
    """Sign file using OpenSSL"""
    sig_file = file_path + '.sig'
    
    try:
        subprocess.run([
            'openssl', 'dgst', '-sha256',
            '-sign', private_key_path,
            '-out', sig_file,
            file_path
        ], check=True, capture_output=True)
        
        # Read signature
        with open(sig_file, 'rb') as f:
            signature = base64.b64encode(f.read()).decode('utf-8')
        
        # Cleanup temp file
        os.remove(sig_file)
        
        return signature
        
    except subprocess.CalledProcessError as e:
        print(f"Error signing file: {e.stderr.decode()}", file=sys.stderr)
        return None

def extract_cert_info(cert_path):
    """Extract issuer and subject from certificate"""
    try:
        # Get issuer
        result = subprocess.run([
            'openssl', 'x509', '-in', cert_path,
            '-noout', '-issuer'
        ], check=True, capture_output=True, text=True)
        issuer = result.stdout.strip().replace('issuer=', '')
        
        # Get subject
        result = subprocess.run([
            'openssl', 'x509', '-in', cert_path,
            '-noout', '-subject'
        ], check=True, capture_output=True, text=True)
        subject = result.stdout.strip().replace('subject=', '')
        
        return issuer, subject
        
    except subprocess.CalledProcessError as e:
        print(f"Error reading certificate: {e.stderr}", file=sys.stderr)
        return None, None

def read_certificate(cert_path):
    """Read certificate PEM file"""
    with open(cert_path, 'r') as f:
        return f.read()

def create_metadata(plugin_path, signature, cert_path, sha256_hash):
    """Create plugin metadata JSON"""
    
    issuer, subject = extract_cert_info(cert_path)
    certificate_pem = read_certificate(cert_path)
    
    if not issuer or not subject:
        return None
    
    plugin_name = Path(plugin_path).stem
    
    metadata = {
        "plugin": {
            "name": plugin_name,
            "version": "1.0.0",  # Should be extracted from plugin or passed as argument
            "author": "ThemisDB Team",
            "description": f"Hardware acceleration plugin: {plugin_name}",
            "license": "MIT",
            "build_date": datetime.utcnow().isoformat() + "Z",
            
            "signature": {
                "sha256": sha256_hash,
                "signature": signature,
                "certificate": certificate_pem,
                "issuer": issuer,
                "subject": subject,
                "timestamp": int(datetime.now().timestamp()),
                "algorithm": "RSA-SHA256"
            },
            
            "permissions": [
                "gpu_access",
                "memory_access"
            ]
        }
    }
    
    return metadata

def main():
    if len(sys.argv) < 4:
        print("Usage: sign_plugin.py <plugin_file> <private_key> <certificate> [--output metadata.json]")
        sys.exit(1)
    
    plugin_path = sys.argv[1]
    private_key_path = sys.argv[2]
    cert_path = sys.argv[3]
    
    output_path = plugin_path + '.json'
    if '--output' in sys.argv:
        idx = sys.argv.index('--output')
        if idx + 1 < len(sys.argv):
            output_path = sys.argv[idx + 1]
    
    # Check files exist
    if not os.path.exists(plugin_path):
        print(f"Error: Plugin file not found: {plugin_path}", file=sys.stderr)
        sys.exit(1)
    
    if not os.path.exists(private_key_path):
        print(f"Error: Private key not found: {private_key_path}", file=sys.stderr)
        sys.exit(1)
    
    if not os.path.exists(cert_path):
        print(f"Error: Certificate not found: {cert_path}", file=sys.stderr)
        sys.exit(1)
    
    print(f"Signing plugin: {plugin_path}")
    
    # Calculate hash
    print("Calculating SHA-256 hash...")
    sha256_hash = calculate_sha256(plugin_path)
    print(f"  Hash: {sha256_hash}")
    
    # Sign file
    print("Creating digital signature...")
    signature = sign_file(plugin_path, private_key_path)
    if not signature:
        print("Error: Failed to create signature", file=sys.stderr)
        sys.exit(1)
    print(f"  Signature: {signature[:32]}... ({len(signature)} bytes)")
    
    # Create metadata
    print("Creating metadata...")
    metadata = create_metadata(plugin_path, signature, cert_path, sha256_hash)
    if not metadata:
        print("Error: Failed to create metadata", file=sys.stderr)
        sys.exit(1)
    
    # Write metadata
    print(f"Writing metadata to: {output_path}")
    with open(output_path, 'w') as f:
        json.dump(metadata, f, indent=2)
    
    print(f"\n✅ Plugin signed successfully!")
    print(f"   Plugin: {plugin_path}")
    print(f"   Hash: {sha256_hash}")
    print(f"   Metadata: {output_path}")
    print(f"\nDistribute both files together:")
    print(f"  - {os.path.basename(plugin_path)}")
    print(f"  - {os.path.basename(output_path)}")

if __name__ == '__main__':
    main()
