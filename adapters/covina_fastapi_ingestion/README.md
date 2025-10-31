# Covina → THEMIS FastAPI Ingestion (Minimal Adapter)

Diese kleine FastAPI-App dient als Ingestion-Adapter ohne UDS3-Framework. Sie extrahiert/transformiert Daten minimal (z. B. Text), erzeugt optional Embeddings und sendet strukturierte JSON-Payloads an THEMIS `POST /content/import`.

## Features
- Endpunkte:
  - `POST /ingest/file` – Datei-Upload (z. B. text/plain) → Content/Chunks/Edges erzeugen → an THEMIS senden
  - `POST /ingest/json` – Bereits strukturierte Payload direkt an THEMIS weiterreichen
- Optional: einfache Embedding-Erzeugung (ohne schwere ML-Abhängigkeiten). Wenn echte Embeddings gewünscht sind, siehe Abschnitt "Echte Embeddings" unten.
- Konfiguration über Umgebungsvariablen

## Voraussetzungen
- Python 3.10+
- Eine laufende THEMIS-Instanz (Standard: http://127.0.0.1:8765)

## Schnellstart (Windows PowerShell)

```powershell
cd adapters\covina_fastapi_ingestion
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
$env:THEMIS_URL = "http://127.0.0.1:8765"
$env:ENABLE_EMBEDDINGS = "true"   # oder "false"
uvicorn app:app --host 127.0.0.1 --port 8001 --reload
```

Test (Textdatei):
```powershell
# Beispiel: sendet eine Textdatei an /ingest/file
Invoke-WebRequest -Uri http://127.0.0.1:8001/ingest/file -Method POST -InFile ..\..\README.md -ContentType "text/plain" | Select-Object -Expand Content
```

Test (Direkt-Payload):
```powershell
$body = @{ content = @{ mime_type = "text/plain"; tags = @("demo") }; chunks = @(@{ seq_num = 0; chunk_type = "text"; text = "hello world" }) } | ConvertTo-Json -Depth 5
Invoke-RestMethod -Uri http://127.0.0.1:8001/ingest/json -Method POST -Body $body -ContentType 'application/json'
```

## Konfiguration
- `THEMIS_URL` (Default: `http://127.0.0.1:8765`)
- `ENABLE_EMBEDDINGS` ("true"/"false", Default: true)

## Echte Embeddings (optional)
Standardmäßig erzeugt der Adapter leichte, deterministische Pseudo-Embeddings (ohne ML-Abhängigkeiten), damit ihr sofort starten könnt. Für echte semantische Embeddings (z. B. all-MiniLM-L6-v2) könnt ihr optional installieren:

```powershell
pip install sentence-transformers
```

Der Adapter erkennt `sentence-transformers` automatisch und verwendet sie, wenn vorhanden. Andernfalls fällt er auf die leichte Hash-basierte Variante zurück.

## Struktur
- `app.py` – FastAPI-Anwendung, Endpunkte und Orchestrierung
- `themis_client.py` – HTTP-Client zu THEMIS `/content/import`
- `processors/text.py` – Minimaler Text-Prozessor (Chunking + Embeddings)

## Hinweise
- Embedding-Dimension wird beim ersten Insert im THEMIS-Vectorindex festgelegt. Achtet auf Konsistenz.
- Für komplexe Modalitäten (Bilder/Video/Audio/CAD/Geo) erweitert ihr die `processors/` um passende Module und erzeugt eine kanonische Payload gemäß `docs/content/ingestion.md`.
