# Build MkDocs Documentation Locally
# Erstellt die MkDocs HTML-Dokumentation lokal im site/ Verzeichnis

Write-Host "=== ThemisDB Documentation Build ===" -ForegroundColor Cyan

# Prüfe ob Python und pip verfügbar sind
$pythonCmd = Get-Command python -ErrorAction SilentlyContinue
if (-not $pythonCmd) {
    Write-Error "Python nicht gefunden! Bitte Python 3.x installieren."
    exit 1
}

Write-Host "Python Version: $(python --version)" -ForegroundColor Gray

# Prüfe/Installiere MkDocs Dependencies
Write-Host "`nInstalliere/Aktualisiere MkDocs Dependencies..." -ForegroundColor Green
if (Test-Path "requirements-docs.txt") {
    pip install -r requirements-docs.txt --upgrade --quiet
} else {
    Write-Host "requirements-docs.txt nicht gefunden, installiere Basis-Pakete..." -ForegroundColor Yellow
    pip install mkdocs mkdocs-material mkdocs-print-site-plugin --upgrade --quiet
}

# Build Documentation
Write-Host "`nBaue Dokumentation..." -ForegroundColor Green
mkdocs build --clean

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✅ Dokumentation erfolgreich gebaut!" -ForegroundColor Green
    Write-Host "   Output: .\site\" -ForegroundColor Gray
    Write-Host "`nÖffne die Dokumentation lokal:" -ForegroundColor Cyan
    Write-Host "   .\site\index.html" -ForegroundColor White
    
    # Optional: Öffne im Browser
    $openBrowser = Read-Host "`nMöchtest du die Dokumentation im Browser öffnen? (j/n)"
    if ($openBrowser -eq 'j' -or $openBrowser -eq 'J' -or $openBrowser -eq 'y' -or $openBrowser -eq 'Y') {
        Start-Process ".\site\index.html"
    }
} else {
    Write-Error "Build fehlgeschlagen!"
    exit 1
}
