# ThemisDB Plugin Signer

Tool zum Signieren von Hardware-Beschleunigungsplugins mit digitalen Signaturen.

## Verwendung

### Plugin signieren

```bash
python sign_plugin.py <plugin_file> <private_key> <certificate>
```

**Beispiel:**
```bash
python sign_plugin.py \
  ../../plugins/themis_accel_cuda.dll \
  certs/themis_plugin_key.pem \
  certs/themis_plugin_cert.pem
```

**Output:**
```
themis_accel_cuda.dll       # Original Plugin
themis_accel_cuda.dll.json  # Signatur-Metadata
```

### Zertifikate erstellen (für Entwicklung)

```bash
# 1. Private Key
openssl genrsa -out themis_plugin_key.pem 4096

# 2. Self-signed Certificate (1 Jahr gültig)
openssl req -new -x509 -key themis_plugin_key.pem \
  -out themis_plugin_cert.pem -days 365 \
  -subj "/CN=ThemisDB Official Plugins/O=ThemisDB/C=DE"
```

### Batch-Signierung

```bash
# Alle Plugins im Verzeichnis signieren
for plugin in ../../plugins/*.dll; do
  python sign_plugin.py "$plugin" \
    certs/themis_plugin_key.pem \
    certs/themis_plugin_cert.pem
done
```

## Metadata-Format

Generiert JSON-Datei mit:
- SHA-256 Hash des Plugins
- RSA/ECDSA Signatur
- X.509 Zertifikat
- Issuer/Subject
- Timestamp

Siehe `docs/security/PLUGIN_SECURITY.md` für Details.

## Sicherheitshinweise

⚠️ **Private Keys schützen!**
- Niemals in Git committen
- Secure Storage (HSM, Vault)
- Rotation alle 12 Monate

⚠️ **Production Zertifikate**
- Von vertrauenswürdiger CA signieren lassen
- Nicht self-signed verwenden
- Code Signing Certificate verwenden

## Weitere Informationen

- [Plugin Security Dokumentation](../../docs/security/PLUGIN_SECURITY.md)
- [Plugin Development Guide](../../plugins/README.md)
