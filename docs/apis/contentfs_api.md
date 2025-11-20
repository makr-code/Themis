# ContentFS HTTP API

Schnittstelle zum Speichern, Abrufen und Löschen binärer Inhalte (Blobs) mit integren Metadaten. Unterstützt ETags (SHA‑256) und Byte-Range Downloads.

- Basis-URL: `http://<host>:<port>`
- Ressourcenpfad: `/contentfs/:pk`
- Auth: Scopes + Policies erforderlich (siehe Sicherheit & Access Control)

## Endpunkte

- PUT `/contentfs/:pk`
  - Zweck: Blob hochladen oder überschreiben
  - Header:
    - `Content-Type`: MIME-Typ (z. B. `application/octet-stream`)
    - Optional `X-Checksum-SHA256`: erwarteter SHA‑256 des Request-Bodys (Hex)
  - Body: binär (roh)
  - Antworten:
    - 201 Created
      - Header: `ETag` (SHA‑256), `Location: /contentfs/:pk`
      - Body: JSON `{ pk, mime, size, sha256_hex }`
    - 400 Bad Request (Checksum mismatch)
    - 500 Internal Server Error (Speicherfehler)

- HEAD `/contentfs/:pk`
  - Zweck: Metadaten abrufen
  - Antworten:
    - 200 OK
      - Header: `Content-Length`, `Content-Type`, `ETag`, `Accept-Ranges: bytes`
    - 404 Not Found

- GET `/contentfs/:pk`
  - Zweck: Blob abrufen (vollständig oder als Byte-Range)
  - Header (optional):
    - `Range: bytes=<start>-<end>` (einzelne Range)
  - Antworten:
    - 200 OK (vollständiger Inhalt)
    - 206 Partial Content (bei Range)
      - Header: `Content-Range: bytes <start>-<end>/<total>`, `Accept-Ranges: bytes`
    - 404 Not Found
    - 416 Range Not Satisfiable (ungültiger Bereich)

- DELETE `/contentfs/:pk`
  - Zweck: Blob + Metadaten löschen
  - Antworten:
    - 204 No Content
    - 404 Not Found (wenn weder Meta noch Blob existiert)

## Beispiele

### Upload mit Checksumme
```http
PUT /contentfs/report_2025.bin HTTP/1.1
Content-Type: application/octet-stream
X-Checksum-SHA256: 1e0023...abcd

<binary>
```
Antwort:
```http
HTTP/1.1 201 Created
ETag: 1e0023...abcd
Location: /contentfs/report_2025.bin
Content-Type: application/json

{"pk":"report_2025.bin","mime":"application/octet-stream","size":12345,"sha256_hex":"1e0023...abcd"}
```

### Range-Download (erste 100 Bytes)
```http
GET /contentfs/report_2025.bin HTTP/1.1
Range: bytes=0-99
```
Antwort:
```http
HTTP/1.1 206 Partial Content
Content-Range: bytes 0-99/12345
Accept-Ranges: bytes
Content-Type: application/octet-stream

<100 bytes>
```

### cURL Beispiele

Upload (mit Content-Type und Checksumme):
```bash
curl -X PUT \
  -H "Content-Type: application/octet-stream" \
  -H "X-Checksum-SHA256: $(sha256sum file.bin | awk '{print $1}')" \
  --data-binary @file.bin \
  http://localhost:8765/contentfs/file.bin
```

Metadaten (HEAD):
```bash
curl -I http://localhost:8765/contentfs/file.bin
```

Vollständiger Download:
```bash
curl -o out.bin http://localhost:8765/contentfs/file.bin
```

Range-Download (Bytes 0-99):
```bash
curl -H "Range: bytes=0-99" -o first100.bin http://localhost:8765/contentfs/file.bin
```

Löschen:
```bash
curl -X DELETE -i http://localhost:8765/contentfs/file.bin
```

### PowerShell Integrations-Test

Ein vollständiger End-to-End-Test befindet sich in `test_content_fs_api_integration.ps1`. Er prüft Upload, HEAD, Voll-Download, Range-Download und Delete.
Ausführen (bei laufendem Server):
```powershell
./test_content_fs_api_integration.ps1
```

## Fehlercodes
- 400: Checksumme abweichend oder ungültige Parameter
- 404: Ressource nicht vorhanden
- 416: Range ungültig / außerhalb der Größe
- 500: Interner Fehler (Persistenz)

## Sicherheit & Access Control

- Scopes (AuthMiddleware Pflicht):
  - `data:read` für `GET` und `HEAD`
  - `data:write` für `PUT` und `DELETE`
- Policy Engine (Ranger/Policies):
  - Actions: `content.read`, `content.write`, `content.delete`
  - Resource Path: `/contentfs/:pk` (voller Request-Pfad)
- Evaluationsreihenfolge:
  - AuthN → Scopes → Policies. Erst bei Erfolg erfolgt die Handlerausführung.
- Fehlerbilder:
  - Fehlende/ungültige Auth → `401 Unauthorized`
  - Fehlende Scopes oder verweigerte Policy → `403 Forbidden`
- Governance:
  - Governance-Header (`X-Themis-*`) werden bei Antworten gesetzt.

## Konfiguration

Die ContentFS-Konfiguration ist über folgende Endpunkte abrufbar/änderbar:

- GET `/content/config`
  - Liefert das aktuell effektive Konfigurationsobjekt.
- PUT `/content/config`
  - Aktualisiert Konfigurationsteile. Änderungen wirken nur für neue Uploads.

Aktuelle Felder (Auszug):

- `chunk_size_bytes` (uint, optional):
  - Gültiger Bereich: 65.536 (64 KiB) bis 16.777.216 (16 MiB)
  - Wirkt sich auf die Chunk-Größe für neue Blobs aus (Range-Reads werden dadurch effizienter, bestehende Inhalte bleiben unverändert).

Beispiele:

GET:
```http
GET /content/config HTTP/1.1
```
Antwort (Beispiel):
```json
{
  "compress_blobs": false,
  "compression_level": 19,
  "skip_compressed_mimes": ["image/", "video/", "application/zip", "application/gzip"],
  "chunk_size_bytes": 1048576
}
```

PUT (Chunk-Größe auf 2 MiB setzen):
```http
PUT /content/config HTTP/1.1
Content-Type: application/json

{"chunk_size_bytes": 2097152}
```
Antwort:
```json
{"status":"ok","chunk_size_bytes":2097152,"note":"Configuration updated. Changes apply to new content imports only."}
```

## Implementierungsdetails
- Storage-Keys: `content:<pk>:meta` (CBOR-JSON), `content:<pk>:blob`
- ETag: SHA‑256 des Blobs (Hex)
- MIME: aus `Content-Type` des Uploads; Default `application/octet-stream`
