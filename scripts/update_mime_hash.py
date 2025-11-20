#!/usr/bin/env python3
"""Update the integrity.hash field in config/mime_types.yaml using the same
canonical serialization logic as MimeDetector::computeDeterministicHash().

Requires: pip install pyyaml
Usage:
  python scripts/update_mime_hash.py [path/to/mime_types.yaml]
"""
import sys, hashlib, yaml, os
from datetime import datetime, timezone

def canonical_hash(data: dict) -> str:
    # Build deterministic buffer
    buf_parts = []

    # Extensions
    exts = data.get('extensions', {})
    ext_lines = [f"{k}={v}" for k, v in exts.items()]
    buf_parts.append('[extensions]')
    for line in sorted(ext_lines):
        buf_parts.append(line)

    # Magic signatures
    magic = data.get('magic_signatures', [])
    magic_lines = []
    for entry in magic:
        sig_bytes = entry.get('signature', [])
        hex_sig = ''.join(f"{int(b):02x}" for b in sig_bytes)
        offset = entry.get('offset', 0)
        wild = entry.get('wildcard_positions', [])
        wild_part = ''
        if wild:
            wild_part = ':' + ','.join(str(int(w)) for w in sorted(wild))
        magic_lines.append(f"{entry.get('mime_type','')}@{offset}={hex_sig}{wild_part}")
    buf_parts.append('[magic]')
    for line in sorted(magic_lines):
        buf_parts.append(line)

    # Categories
    categories = data.get('categories', {})
    cat_lines = []
    for cat, mimes in categories.items():
        sorted_mimes = sorted(mimes)
        cat_lines.append(f"{cat}={','.join(sorted_mimes)}")
    buf_parts.append('[categories]')
    for line in sorted(cat_lines):
        buf_parts.append(line)

    buffer = '\n'.join(buf_parts) + '\n'
    digest = hashlib.sha256(buffer.encode('utf-8')).hexdigest()
    return digest

def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'config/mime_types.yaml'
    if not os.path.exists(path):
        print(f"File not found: {path}", file=sys.stderr)
        sys.exit(1)
    with open(path, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)
    new_hash = canonical_hash(data)
    if 'integrity' not in data:
        data['integrity'] = {}
    data['integrity']['algorithm'] = 'sha256'
    data['integrity']['hash'] = new_hash
    data['integrity']['generated_at'] = datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    scope = ['extensions', 'magic_signatures', 'categories']
    data['integrity']['scope'] = scope
    with open(path, 'w', encoding='utf-8') as f:
        yaml.safe_dump(data, f, sort_keys=False, allow_unicode=True)
    print(f"Updated integrity hash to {new_hash}")

if __name__ == '__main__':
    main()
