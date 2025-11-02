#!/usr/bin/env python3
"""
PII Detection Engine Signer

This tool signs PII detection engine configurations with PKI signatures.
Used during development/deployment to create trusted plugin configurations.

Usage:
    python sign_pii_engine.py --config pii_patterns.yaml --engine regex --output signed_config.yaml

Security Flow:
    1. Extract engine configuration (excluding signature block)
    2. Compute SHA-256 hash of normalized JSON
    3. Sign hash with PKI private key
    4. Inject signature metadata into YAML
    5. Write signed configuration

Requirements:
    - PKI private key access
    - OpenSSL or cryptography library
    - YAML parser (pyyaml)
"""

import argparse
import hashlib
import json
import yaml
import base64
from datetime import datetime, timezone
from pathlib import Path

try:
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import rsa, padding
    from cryptography.hazmat.backends import default_backend
except ImportError:
    print("ERROR: cryptography library required")
    print("Install with: pip install cryptography pyyaml")
    exit(1)


def compute_config_hash(config: dict) -> str:
    """
    Compute SHA-256 hash of engine configuration.
    
    Args:
        config: Engine configuration dict (without signature block)
    
    Returns:
        Hex-encoded SHA-256 hash
    """
    # Remove signature block if present
    config_copy = config.copy()
    if 'signature' in config_copy:
        del config_copy['signature']
    
    # Normalize to JSON (deterministic serialization)
    normalized = json.dumps(config_copy, sort_keys=True, separators=(',', ':'))
    
    # Compute SHA-256
    hash_obj = hashlib.sha256(normalized.encode('utf-8'))
    return hash_obj.hexdigest()


def sign_hash(config_hash: str, private_key_path: str, passphrase: str = None) -> str:
    """
    Sign configuration hash with PKI private key.
    
    Args:
        config_hash: Hex-encoded SHA-256 hash
        private_key_path: Path to PEM-encoded private key
        passphrase: Optional passphrase for encrypted keys
    
    Returns:
        Base64-encoded signature

    Security note:
        - The 'passphrase' parameter is supplied at runtime (CLI/env) and must not be hardcoded or stored in the repository.
        - Do not log secrets. This tool avoids printing the passphrase.
    """
    # Load private key
    with open(private_key_path, 'rb') as f:
        private_key = serialization.load_pem_private_key(
            f.read(),
            password=passphrase.encode('utf-8') if passphrase else None,
            backend=default_backend()
        )
    
    # Convert hex hash to bytes
    hash_bytes = bytes.fromhex(config_hash)
    
    # Sign hash (PKCS#1 v1.5 padding for compatibility)
    signature = private_key.sign(
        hash_bytes,
        padding.PKCS1v15(),
        hashes.SHA256()
    )
    
    # Encode as base64
    return base64.b64encode(signature).decode('ascii')


def inject_signature(config: dict, signature_data: dict) -> dict:
    """
    Inject signature metadata into engine configuration.
    
    Args:
        config: Original engine configuration
        signature_data: Signature metadata dict
    
    Returns:
        Configuration with signature block
    """
    config_copy = config.copy()
    config_copy['signature'] = signature_data
    return config_copy


def sign_engine(yaml_path: str, engine_type: str, private_key_path: str, 
                output_path: str, passphrase: str = None, signer: str = "VCC Security Team"):
    """
    Sign a PII detection engine configuration.
    
    Args:
        yaml_path: Path to pii_patterns.yaml
        engine_type: Engine type to sign (e.g., "regex")
        private_key_path: Path to PKI private key
        output_path: Path for signed output YAML
        passphrase: Optional key passphrase
        signer: Entity signing the configuration
    """
    # Load YAML
    with open(yaml_path, 'r') as f:
        config = yaml.safe_load(f)
    
    # Find engine configuration
    engine_config = None
    engine_index = None
    for i, engine in enumerate(config.get('detection_engines', [])):
        if engine.get('type') == engine_type:
            engine_config = engine
            engine_index = i
            break
    
    if not engine_config:
        raise ValueError(f"Engine '{engine_type}' not found in {yaml_path}")
    
    print(f"[*] Found {engine_type} engine configuration")
    
    # Compute configuration hash
    config_hash = compute_config_hash(engine_config)
    print(f"[*] Configuration hash (SHA-256): {config_hash}")
    
    # Sign hash
    signature = sign_hash(config_hash, private_key_path, passphrase)
    print(f"[*] Generated signature: {signature[:64]}...")
    
    # Create signature metadata
    signature_data = {
        'config_hash': config_hash,
        'signature': signature,
        'signature_id': f"pii-{engine_type}-engine-v{engine_config.get('version', '1.0.0')}",
        'cert_serial': 'VCC-PKI-001',  # TODO: Extract from certificate
        'signed_at': datetime.now(timezone.utc).isoformat(),
        'signer': signer
    }
    
    # Inject signature
    signed_config = inject_signature(engine_config, signature_data)
    config['detection_engines'][engine_index] = signed_config
    
    # Write signed YAML
    with open(output_path, 'w') as f:
        yaml.dump(config, f, default_flow_style=False, sort_keys=False)
    
    print(f"[✓] Signed configuration written to {output_path}")
    print(f"[✓] Signature ID: {signature_data['signature_id']}")
    print(f"[✓] Signed by: {signer}")
    print(f"[✓] Signed at: {signature_data['signed_at']}")


def generate_test_key_pair(output_dir: str = "."):
    """
    Generate test RSA key pair for development.
    
    WARNING: Only use for testing! Production should use HSM or secure key storage.
    
    Args:
        output_dir: Directory to write private_key.pem and public_key.pem
    """
    # Generate 2048-bit RSA key
    private_key = rsa.generate_private_key(
        public_exponent=65537,
        key_size=2048,
        backend=default_backend()
    )
    
    # Write private key
    private_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption()
    )
    private_path = Path(output_dir) / "private_key.pem"
    with open(private_path, 'wb') as f:
        f.write(private_pem)
    
    # Write public key
    public_key = private_key.public_key()
    public_pem = public_key.public_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PublicFormat.SubjectPublicKeyInfo
    )
    public_path = Path(output_dir) / "public_key.pem"
    with open(public_path, 'wb') as f:
        f.write(public_pem)
    
    print(f"[✓] Generated test key pair:")
    print(f"    Private key: {private_path}")
    print(f"    Public key: {public_path}")
    print(f"[!] WARNING: These are test keys. Use HSM for production!")


def main():
    parser = argparse.ArgumentParser(
        description="Sign PII detection engine configurations with PKI"
    )
    
    subparsers = parser.add_subparsers(dest='command', required=True)
    
    # Sign command
    sign_parser = subparsers.add_parser('sign', help='Sign engine configuration')
    sign_parser.add_argument('--config', required=True, help='Path to pii_patterns.yaml')
    sign_parser.add_argument('--engine', required=True, help='Engine type (e.g., regex, ner)')
    sign_parser.add_argument('--key', required=True, help='Path to private key PEM file')
    sign_parser.add_argument('--output', required=True, help='Output path for signed YAML')
    # The passphrase should be provided interactively or via a secure environment variable.
    # Never hardcode secrets or store them in files under version control.
    sign_parser.add_argument('--passphrase', help='Private key passphrase (if encrypted)')
    sign_parser.add_argument('--signer', default='VCC Security Team', help='Signer entity name')
    
    # Generate test keys command
    keygen_parser = subparsers.add_parser('keygen', help='Generate test RSA key pair')
    keygen_parser.add_argument('--output-dir', default='.', help='Output directory for keys')
    
    args = parser.parse_args()
    
    if args.command == 'sign':
        sign_engine(
            args.config,
            args.engine,
            args.key,
            args.output,
            args.passphrase,
            args.signer
        )
    elif args.command == 'keygen':
        generate_test_key_pair(args.output_dir)


if __name__ == '__main__':
    main()
