# Script to prepare ThemisDB for release packaging
# This script updates version numbers across all packaging files

param(
    [Parameter(Mandatory=$true)]
    [string]$Version
)

# Remove 'v' prefix if present
$Version = $Version.TrimStart('v')

Write-Host "Preparing ThemisDB v$Version for release packaging..." -ForegroundColor Green

# Update CMakeLists.txt version
Write-Host "Updating CMakeLists.txt..."
$cmakeContent = Get-Content CMakeLists.txt -Raw
$cmakeContent = $cmakeContent -replace 'project\(Themis VERSION [0-9.]+ LANGUAGES CXX\)', "project(Themis VERSION $Version LANGUAGES CXX)"
$cmakeContent | Set-Content CMakeLists.txt

# Update vcpkg.json version
Write-Host "Updating vcpkg.json..."
$vcpkgContent = Get-Content vcpkg.json -Raw
$vcpkgContent = $vcpkgContent -replace '"version":\s*"[0-9.]+"', """version"": ""$Version"""
$vcpkgContent | Set-Content vcpkg.json

# Update Chocolatey nuspec
Write-Host "Updating packaging/chocolatey/themisdb.nuspec..."
$nuspecContent = Get-Content packaging/chocolatey/themisdb.nuspec -Raw
$nuspecContent = $nuspecContent -replace '<version>[0-9.]+</version>', "<version>$Version</version>"
$nuspecContent | Set-Content packaging/chocolatey/themisdb.nuspec

# Update Chocolatey install script
Write-Host "Updating packaging/chocolatey/tools/chocolateyinstall.ps1..."
$chocoInstall = Get-Content packaging/chocolatey/tools/chocolateyinstall.ps1 -Raw
$chocoInstall = $chocoInstall -replace '\$version = ''[0-9.]+''', "`$version = '$Version'"
$chocoInstall | Set-Content packaging/chocolatey/tools/chocolateyinstall.ps1

# Update WinGet manifests
Write-Host "Updating WinGet manifests..."
$wingetDir = "packaging/winget/manifests/t/ThemisDB/ThemisDB"
if (Test-Path $wingetDir) {
    $newVersionDir = Join-Path $wingetDir $Version
    New-Item -ItemType Directory -Force -Path $newVersionDir | Out-Null
    
    # Find the latest version directory
    $latestDir = Get-ChildItem $wingetDir -Directory | 
                 Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } | 
                 Sort-Object Name -Descending | 
                 Select-Object -First 1
    
    if ($latestDir) {
        # Copy and update manifests
        Get-ChildItem $latestDir.FullName -Filter "*.yaml" | ForEach-Object {
            $content = Get-Content $_.FullName -Raw
            $content = $content -replace 'PackageVersion: [0-9.]+', "PackageVersion: $Version"
            $content = $content -replace 'v[0-9.]+/', "v$Version/"
            $content | Set-Content (Join-Path $newVersionDir $_.Name)
        }
    }
}

# Update RPM spec (if on WSL or Linux)
if (Test-Path themisdb.spec) {
    Write-Host "Updating themisdb.spec..."
    $specContent = Get-Content themisdb.spec -Raw
    $specContent = $specContent -replace 'Version:\s+[0-9.]+', "Version:        $Version"
    $specContent | Set-Content themisdb.spec
}

# Update PKGBUILD (if on WSL or Linux)
if (Test-Path PKGBUILD) {
    Write-Host "Updating PKGBUILD..."
    $pkgContent = Get-Content PKGBUILD -Raw
    $pkgContent = $pkgContent -replace 'pkgver=[0-9.]+', "pkgver=$Version"
    $pkgContent = $pkgContent -replace 'pkgrel=[0-9]+', "pkgrel=1"
    $pkgContent | Set-Content PKGBUILD
}

# Update Homebrew formula
if (Test-Path packaging/homebrew/themisdb.rb) {
    Write-Host "Updating packaging/homebrew/themisdb.rb..."
    $brewContent = Get-Content packaging/homebrew/themisdb.rb -Raw
    $brewContent = $brewContent -replace 'url "https://github\.com/makr-code/ThemisDB/archive/refs/tags/v[0-9.]+\.tar\.gz"', 
                                         "url ""https://github.com/makr-code/ThemisDB/archive/refs/tags/v$Version.tar.gz"""
    $brewContent | Set-Content packaging/homebrew/themisdb.rb
}

Write-Host ""
Write-Host "Version updated to $Version in all packaging files." -ForegroundColor Green
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Review changes: git diff"
Write-Host "2. Update CHANGELOG.md with release notes"
Write-Host "3. Commit changes: git add . && git commit -m 'Bump version to $Version'"
Write-Host "4. Create git tag: git tag -a v$Version -m 'Release version $Version'"
Write-Host "5. Push changes: git push && git push --tags"
Write-Host "6. Create GitHub release to trigger package builds"
Write-Host ""
Write-Host "To calculate source tarball SHA256:" -ForegroundColor Cyan
Write-Host "  Invoke-WebRequest https://github.com/makr-code/ThemisDB/archive/v$Version.tar.gz -OutFile v$Version.tar.gz"
Write-Host "  Get-FileHash v$Version.tar.gz -Algorithm SHA256"
Write-Host "  # Update the hash in PKGBUILD and packaging/homebrew/themisdb.rb"
