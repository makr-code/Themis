# Sync Documentation to GitHub Wiki
# Dieses Script synchronisiert die docs/ Markdown-Dateien mit dem GitHub Wiki

param(
    [string]$WikiRepo = "https://github.com/makr-code/ThemisDB.wiki.git",
    [string]$DocsPath = ".\docs",
    [string]$WikiPath = ".\wiki-temp",
    [switch]$CleanupAfter = $true
)

Write-Host "=== ThemisDB Documentation -> Wiki Sync ===" -ForegroundColor Cyan

# Prüfe ob docs/ existiert
if (-not (Test-Path $DocsPath)) {
    Write-Error "Dokumentationsverzeichnis nicht gefunden: $DocsPath"
    exit 1
}

# Cleanup old wiki clone if exists
if (Test-Path $WikiPath) {
    Write-Host "Lösche altes Wiki-Verzeichnis..." -ForegroundColor Yellow
    Remove-Item -Path $WikiPath -Recurse -Force
}

# Clone Wiki Repository
Write-Host "Clone Wiki Repository..." -ForegroundColor Green
git clone $WikiRepo $WikiPath
if ($LASTEXITCODE -ne 0) {
    Write-Error "Wiki-Clone fehlgeschlagen. Stelle sicher, dass das Wiki auf GitHub aktiviert ist!"
    Write-Host "Aktiviere das Wiki auf: https://github.com/makr-code/ThemisDB/wiki" -ForegroundColor Yellow
    exit 1
}

# Synchronisiere Markdown-Dateien
Write-Host "Synchronisiere Markdown-Dateien..." -ForegroundColor Green

# Lösche alte Dateien im Wiki (außer .git)
Get-ChildItem -Path $WikiPath -Exclude ".git" | Remove-Item -Recurse -Force

# Kopiere alle Markdown-Dateien (rekursiv, Struktur beibehalten)
$mdFiles = Get-ChildItem -Path $DocsPath -Filter "*.md" -Recurse
$totalFiles = $mdFiles.Count
$counter = 0

foreach ($file in $mdFiles) {
    $counter++
    $relativePath = $file.FullName.Substring($DocsPath.Length + 1)
    $targetPath = Join-Path $WikiPath $relativePath
    $targetDir = Split-Path $targetPath -Parent
    
    # Erstelle Zielverzeichnis falls nötig
    if (-not (Test-Path $targetDir)) {
        New-Item -Path $targetDir -ItemType Directory -Force | Out-Null
    }
    
    Copy-Item -Path $file.FullName -Destination $targetPath -Force
    Write-Progress -Activity "Kopiere Dateien" -Status "$counter von $totalFiles" -PercentComplete (($counter / $totalFiles) * 100)
}

Write-Progress -Activity "Kopiere Dateien" -Completed

# Kopiere auch wichtige YAML-Dateien für Kontext
if (Test-Path "mkdocs.yml") {
    Copy-Item "mkdocs.yml" -Destination $WikiPath -Force
    Write-Host "  mkdocs.yml kopiert" -ForegroundColor Gray
}

# Erstelle Wiki-Home.md falls nicht vorhanden
$homePath = Join-Path $WikiPath "Home.md"
if (-not (Test-Path $homePath)) {
    if (Test-Path (Join-Path $DocsPath "index.md")) {
        Copy-Item (Join-Path $DocsPath "index.md") -Destination $homePath -Force
        Write-Host "  Home.md erstellt aus index.md" -ForegroundColor Gray
    }
}

# Git Commit & Push
Write-Host "Committe Änderungen ins Wiki..." -ForegroundColor Green
Set-Location $WikiPath

git add .

$changesCount = (git status --porcelain | Measure-Object).Count
if ($changesCount -eq 0) {
    Write-Host "Keine Änderungen zu committen." -ForegroundColor Yellow
    Set-Location ..
    if ($CleanupAfter) {
        Remove-Item -Path $WikiPath -Recurse -Force
    }
    exit 0
}

$commitMsg = "Auto-sync documentation from docs/ ($(Get-Date -Format 'yyyy-MM-dd HH:mm'))"
git commit -m $commitMsg

Write-Host "Pushe zum GitHub Wiki..." -ForegroundColor Green
git push origin master

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✅ Dokumentation erfolgreich ins Wiki synchronisiert!" -ForegroundColor Green
    Write-Host "   $changesCount Datei(en) aktualisiert" -ForegroundColor Gray
    Write-Host "   Wiki URL: https://github.com/makr-code/ThemisDB/wiki" -ForegroundColor Cyan
} else {
    Write-Error "Push fehlgeschlagen!"
    Set-Location ..
    exit 1
}

# Cleanup
Set-Location ..
if ($CleanupAfter) {
    Write-Host "Räume temporäres Verzeichnis auf..." -ForegroundColor Gray
    Remove-Item -Path $WikiPath -Recurse -Force
}

Write-Host "`nFertig! 🎉" -ForegroundColor Green
