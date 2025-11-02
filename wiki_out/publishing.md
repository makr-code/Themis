# Publikation & Ablage

Diese Seite beschreibt den Build der Dokumentation, die Veröffentlichung auf GitHub Pages und einen optionalen PDF-Export für Offline-Nutzung.

## 1) Lokaler Docs-Build

Voraussetzungen: Python 3.x, `pip`, `mkdocs`, `mkdocs-material`.

```powershell
# Optional: Virtuelle Umgebung (empfohlen)
# py -3 -m venv .venv ; .\.venv\Scripts\Activate.ps1

pip install --upgrade pip
pip install mkdocs mkdocs-material

# Aus dem Repo-Root
mkdocs serve
# Browser: http://127.0.0.1:8000

# Produktion (statisches Site-Verzeichnis "site/")
mkdocs build
```

## 2) GitHub Pages Deployment (CI)

Das Repo enthält einen Workflow unter `.github/workflows/docs.yml`, der bei Push auf `main` die Seite baut und per GitHub Pages veröffentlicht.

- Artefakt: `site/`
- Veröffentlichung: `https://<owner>.github.io/<repo>/` (Repository Settings → Pages → Build & Deployment: GitHub Actions)

Falls dein Projektname/Org abweicht, passe optional `site_url` in `mkdocs.yml`.

## 3) Gesamt-PDF / Druckansicht (empfohlen)

Wir erzeugen automatisch eine druckoptimierte Gesamtansicht der gesamten Dokumentation via `mkdocs-print-site-plugin`:

- Lokaler Build: `python -m mkdocs build`
- Ausgabe: `site/print_page/index.html` (enthält alle Seiten, ToC, Nummerierung)
- Aus dem Browser als PDF speichern (Druckdialog: Hintergrundgrafiken aktivieren)

CI ist bereits so konfiguriert, dass diese Seite mitgebaut und veröffentlicht wird. In GitHub Pages findest du sie unter `/print_page/`.

Vorteile:
- Keine nativen WeasyPrint/GTK-Abhängigkeiten (Windows/Linux/Mac kompatibel)
- Einheitliches Styling (Material Theme print CSS)

Optional kannst du weiterhin klassische PDF-Plugins nutzen, beachte jedoch deren Systemabhängigkeiten (WeasyPrint/wkhtmltopdf) und Plattformunterschiede.

## 4) Artefakt-Ablage

- Site-Build: `site/`-Ordner (statisch, kann als Release-Asset oder Pages-Artefakt genutzt werden)
- Optional: ZIP der Doku für Offline-Verteilung

```powershell
Compress-Archive -Path .\site\* -DestinationPath .\site.zip -Force
```

## 5) Troubleshooting

- 404 in Navigation: Prüfe `mkdocs.yml`-Pfade und ob Dateien existieren
- Lokaler Build schlägt fehl: `mkdocs build --verbose` und auf fehlende Plugins achten
- Pages zeigt alte Version: Warte auf Actions-Finish, ggf. Browser-Cache leeren
