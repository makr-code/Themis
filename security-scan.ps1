param(
  [string]$Root = (Split-Path -Parent $MyInvocation.MyCommand.Path)
)

$ErrorActionPreference = "Stop"
Write-Host "Themis Security Scan starting..." -ForegroundColor Cyan

# 1) .NET Vulnerability Scan (requires internet)
try {
  $csprojs = Get-ChildItem -Path $Root -Recurse -Filter *.csproj -ErrorAction SilentlyContinue
  foreach ($proj in $csprojs) {
    Write-Host "dotnet vuln scan: $($proj.FullName)" -ForegroundColor Yellow
    try {
      dotnet list $proj.FullName package --vulnerable | Out-Host
    } catch {
      Write-Warning "dotnet list package failed for $($proj.Name): $_"
    }
  }
} catch {
  Write-Verbose "No .NET projects found or scan skipped."
}

# 2) Quick C/C++ grep for risky APIs (heuristic)
$cppRoot = Join-Path $Root 'src'
if (Test-Path $cppRoot) {
  $cppFiles = Get-ChildItem -Path $cppRoot -Recurse -Include *.cpp,*.cc,*.c,*.h,*.hpp -ErrorAction SilentlyContinue
  $risky = @('strcpy','strcat','sprintf(','vsprintf(','gets(','scanf(','sscanf(','atoi(')
  foreach ($f in $cppFiles) {
    try {
      $content = Get-Content $f.FullName -ErrorAction SilentlyContinue
      foreach ($p in $risky) {
        $matches = Select-String -InputObject $content -Pattern $p -SimpleMatch
        if ($matches) {
          Write-Warning "[C/C++] Potential risky API in $($f.FullName): pattern '$p'"
        }
      }
    } catch {}
  }
} else {
  Write-Verbose "No src directory found for C/C++ scan."
}

# 3) Secret patterns (heuristic)
$secretPatterns = @(
  'AWS_ACCESS_KEY_ID','AWS_SECRET_ACCESS_KEY',
  'BEGIN RSA PRIVATE KEY','BEGIN PRIVATE KEY',
  'password=','pwd=','client_secret','api_key','x-api-key'
)
$repoFiles = Get-ChildItem -Path $Root -Recurse -File -ErrorAction SilentlyContinue |
  Where-Object { $_.Length -lt 5MB } |
  Where-Object { $_.Extension -notin @('.md','.txt','.pdf','.png','.jpg','.jpeg','.gif','.svg') } |
  Where-Object { $_.FullName -notmatch "\\vcpkg_installed\\|\\build\\|\\CMakeFiles\\|\\.git\\|\\dist\\|\\bin\\|\\obj\\|\\x64\\|\\Release\\|\\Debug\\|\\out\\|\\.vs\\" } |
  Where-Object { $_.FullName -ne $MyInvocation.MyCommand.Path } |
  Where-Object { $_.Name -ne 'security-scan.ps1' }
foreach ($file in $repoFiles) {
  try {
    $text = Get-Content $file.FullName -ErrorAction SilentlyContinue
    foreach ($sp in $secretPatterns) {
      if (Select-String -InputObject $text -Pattern $sp) {
        Write-Warning "[Secrets] Pattern '$sp' found in $($file.FullName)"
      }
    }
  } catch {}
}

Write-Host "Themis Security Scan finished." -ForegroundColor Green
