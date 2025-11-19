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
Write-Host "Erzeuge Basis-Wiki-Seiten (_Header, _Sidebar, _Footer)..." -ForegroundColor Green

# Helper: Fuege Link nur hinzu, wenn Datei vorhanden ist
function Add-LinkIfExists {
    param(
        [ref]$Lines,
        [string]$WikiRoot,
        [string]$RelPath,
        [string]$Title
    )
    $target = Join-Path $WikiRoot $RelPath
    if (Test-Path $target) {
        $Lines.Value += "* [$Title]($RelPath)"
    }
}

# _Header.md
$headerPath = Join-Path $WikiPath "_Header.md"
$headerLines = @()
$headerLines += "[ThemisDB](https://github.com/makr-code/ThemisDB) | [[Home]] | [Issues](https://github.com/makr-code/ThemisDB/issues)"
$headerLines += ""
Set-Content -Path $headerPath -Value $headerLines -Encoding UTF8

# _Sidebar.md
$sidebarPath = Join-Path $WikiPath "_Sidebar.md"
$sb = @()
$sb += "## Navigation"
$sb += "* [[Home]]"
$sb += ""

$sb += "### Overview"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "architecture.md" -Title "Architecture"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "roadmap.md" -Title "Roadmap"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "changelog.md" -Title "Changelog"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "glossary.md" -Title "Glossary"
$sb += ""

$sb += "### Getting Started"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "clients/javascript_sdk_quickstart.md" -Title "JavaScript SDK"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "clients/python_sdk_quickstart.md" -Title "Python SDK"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "clients/rust_sdk_quickstart.md" -Title "Rust SDK"
$sb += ""

$sb += "### API"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "apis/openapi.md" -Title "OpenAPI Overview"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "pii_api.md" -Title "PII API"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "query_engine_aql.md" -Title "Query Engine Overview"
$sb += ""

$sb += "### Query Engine"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "aql_syntax.md" -Title "AQL Syntax"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "aql_explain_profile.md" -Title "EXPLAIN/PROFILE"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "aql-hybrid-queries.md" -Title "Hybrid Queries"
$sb += ""

$sb += "### Storage & Indexes"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "indexes.md" -Title "Indexes Overview"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "index_stats_maintenance.md" -Title "Index Stats"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "storage/rocksdb_layout.md" -Title "RocksDB Layout"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "storage/geo_relational_schema.md" -Title "Geo Schema"
$sb += ""

$sb += "### Security"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "security/overview.md" -Title "Overview"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "rbac_authorization.md" -Title "RBAC & Authorization"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "TLS_SETUP.md" -Title "TLS Setup"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "CERTIFICATE_PINNING.md" -Title "Certificate Pinning"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "security/policies.md" -Title "Policies"
$sb += ""

$sb += "### Search"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "search/hybrid_search_design.md" -Title "Hybrid Search Design"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "search/pagination_benchmarks.md" -Title "Pagination Benchmarks"
$sb += ""

$sb += "### Performance"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "performance_benchmarks.md" -Title "Performance Benchmarks"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "compression_benchmarks.md" -Title "Compression Benchmarks"
$sb += ""

$sb += "### Time Series"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "time_series.md" -Title "Time Series Overview"
$sb += ""

$sb += "### Admin Tools"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "admin_tools_admin_guide.md" -Title "Admin Guide"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "admin_tools_user_guide.md" -Title "User Guide"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "admin_tools_feature_matrix.md" -Title "Feature Matrix"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "admin_tools_search_sort_filter.md" -Title "Search/Sort/Filter"
$sb += ""

$sb += "### Observability"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "observability/metrics.md" -Title "Metrics"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "observability/prometheus_metrics.md" -Title "Prometheus"
$sb += ""

$sb += "### Ingestion"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "ingestion/json_ingestion_spec.md" -Title "JSON Ingestion"
$sb += ""

$sb += "### Operations"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "deployment.md" -Title "Deployment"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "operations_runbook.md" -Title "Runbook"
$sb += ""

$sb += "### Development"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "development/README.md" -Title "Developer Guide"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "development/implementation_status.md" -Title "Implementation Status"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "development/roadmap.md" -Title "Dev Roadmap"
$sb += ""

$sb += "### Source Docs"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "src/README.md" -Title "Code Overview"
Add-LinkIfExists -Lines ([ref]$sb) -WikiRoot $WikiPath -RelPath "src/api/README.md" -Title "HTTP API"

Set-Content -Path $sidebarPath -Value $sb -Encoding UTF8

# _Footer.md
$footerPath = Join-Path $WikiPath "_Footer.md"
$footerLines = @()
$footerLines += "ThemisDB Documentation - auto-synced from /docs on $(Get-Date -Format 'yyyy-MM-dd')"
Set-Content -Path $footerPath -Value $footerLines -Encoding UTF8

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
