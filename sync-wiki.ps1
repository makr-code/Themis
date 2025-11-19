# Sync Documentation to GitHub Wiki
# Dieses Script synchronisiert die docs/ Markdown-Dateien mit dem GitHub Wiki

param(
    [string]$WikiRepo = "https://github.com/makr-code/ThemisDB.wiki.git",
    [string]$DocsPath = ".\docs",
    [string]$WikiPath = ".\wiki-temp",
    [switch]$CleanupAfter = $true
)

Write-Host "=== ThemisDB Documentation -> Wiki Sync ===" -ForegroundColor Cyan

# Pruefe ob docs/ existiert
if (-not (Test-Path $DocsPath)) {
    Write-Error "Dokumentationsverzeichnis nicht gefunden: $DocsPath"
    exit 1
}

# Cleanup old wiki clone if exists
if (Test-Path $WikiPath) {
    Write-Host "Loesche altes Wiki-Verzeichnis..." -ForegroundColor Yellow
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

# Loesche alte Dateien im Wiki (ausser .git)
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
    
    # Erstelle Zielverzeichnis falls noetig
    if (-not (Test-Path $targetDir)) {
        New-Item -Path $targetDir -ItemType Directory -Force | Out-Null
    }
    
    Copy-Item -Path $file.FullName -Destination $targetPath -Force
    Write-Progress -Activity "Kopiere Dateien" -Status "$counter von $totalFiles" -PercentComplete (($counter / $totalFiles) * 100)
}

Write-Progress -Activity "Kopiere Dateien" -Completed

# Kopiere auch wichtige YAML-Dateien fuer Kontext
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

# Erzeuge Grundseiten: _Header.md, _Sidebar.md, _Footer.md
# Diese wurden bereits in docs/ erstellt und werden mitkopiert
Write-Host "Wiki-Basisseiten (_Header, _Sidebar, _Footer, Home) werden aus docs/ übernommen..." -ForegroundColor Green

# Prüfe ob die neuen Wiki-Dateien existieren, sonst erstelle Fallback
$requiredWikiFiles = @("_Header.md", "_Sidebar.md", "_Footer.md", "Home.md")
foreach ($file in $requiredWikiFiles) {
    $sourcePath = Join-Path $DocsPath $file
    $targetPath = Join-Path $WikiPath $file
    
    if (Test-Path $sourcePath) {
        # Datei existiert bereits in docs/, wurde schon kopiert
        Write-Host "  $file bereits vorhanden" -ForegroundColor Gray
    } else {
        Write-Host "  Warnung: $file nicht in docs/ gefunden, erstelle Fallback" -ForegroundColor Yellow
        
        # Erstelle Fallback basierend auf Dateiname
        switch ($file) {
            "_Header.md" {
                $content = "[ThemisDB v0.1.0_alpha](https://github.com/makr-code/ThemisDB) | [[Home]] | [Issues](https://github.com/makr-code/ThemisDB/issues)"
                Set-Content -Path $targetPath -Value $content -Encoding UTF8
            }
            "_Sidebar.md" {
                $content = @(
                    "## Navigation",
                    "* [[Home]]",
                    "* [Architecture](architecture.md)",
                    "* [AQL Syntax](aql_syntax.md)",
                    "* [Deployment](deployment_consolidated.md)"
                )
                Set-Content -Path $targetPath -Value $content -Encoding UTF8
            }
            "_Footer.md" {
                $content = "ThemisDB v0.1.0_alpha - Auto-synced from /docs on $(Get-Date -Format 'yyyy-MM-dd')"
                Set-Content -Path $targetPath -Value $content -Encoding UTF8
            }
            "Home.md" {
                # Fallback: Kopiere index.md als Home.md
                $indexPath = Join-Path $DocsPath "index.md"
                if (Test-Path $indexPath) {
                    Copy-Item $indexPath -Destination $targetPath -Force
                    Write-Host "  Home.md aus index.md erstellt" -ForegroundColor Gray
                }
            }
        }
    }
}

# Git Commit & Push
Write-Host "Committe Aenderungen ins Wiki..." -ForegroundColor Green
Set-Location $WikiPath

git add .

$changesCount = (git status --porcelain | Measure-Object).Count
if ($changesCount -eq 0) {
    Write-Host "Keine Aenderungen zu committen." -ForegroundColor Yellow
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
    Write-Host ""
    Write-Host "Dokumentation erfolgreich ins Wiki synchronisiert!" -ForegroundColor Green
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
    Write-Host "Raeume temporaeres Verzeichnis auf..." -ForegroundColor Gray
    Remove-Item -Path $WikiPath -Recurse -Force
}

Write-Host ""
Write-Host "Fertig!" -ForegroundColor Green
