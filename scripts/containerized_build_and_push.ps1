<#
Containerized orchestrator:
- Runs a Windows build either inside a Windows builder container (if requested and image provided) or locally via scripts/windows_build.ps1
- Runs WSL build via scripts/build.sh inside WSL
- On success, builds Docker image locally and optionally pushes it

Requirements:
- Docker installed. To run Windows containers, Docker must be in Windows-Container mode and the builder image must be a Windows container image with MSVC + PowerShell + git + vcpkg.
- For WSL build, WSL must be installed and reachable via `wsl` command.
- scripts/windows_build.ps1, scripts/build.sh and scripts/docker_build_local.sh must exist in repo.

Usage (from Windows PowerShell in repo root):
  # Use a Windows builder container (image must be prepared beforehand)
  .\scripts\containerized_build_and_push.ps1 -Tag 1.2.3 -UseWindowsContainer -WinBuilderImage "myregistry/windows-dev:latest" -DockerTag myrepo/themis:1.2.3 -PushDocker

  # Or run windows build locally and wsl build in WSL, then build and push docker
  .\scripts\containerized_build_and_push.ps1 -Tag 1.2.3 -DockerTag myrepo/themis:1.2.3 -PushDocker

Parameters:
  -Tag <string>               Release tag (default: timestamp)
  -UseWindowsContainer        Use Windows container for the Windows build (requires -WinBuilderImage)
  -WinBuilderImage <string>   Windows builder image name (required if -UseWindowsContainer)
  -SkipWindowsBuild           Skip the Windows build
  -SkipWSLBuild              Skip the WSL build
  -DockerTag <string>         Docker tag to build/push (default: themis:<tag>)
  -PushDocker                Push the Docker image after build
  -NoDockerCache             Pass --no-cache to docker build
  -SkipTests                 Skip test runs in sub-builds
#>

param(
  [string]$Tag = $(Get-Date -Format 'yyyyMMdd-HHmmss'),
  [string]$LinuxTriplet = 'x64-linux',
  [switch]$UseWindowsContainer = $false,
  [string]$WinBuilderImage = $env:WIN_BUILDER_IMAGE,
  [switch]$SkipWindowsBuild = $false,
  [switch]$SkipWSLBuild = $false,
  [string]$DockerTag = '',
  [switch]$PushDocker = $false,
  [switch]$NoDockerCache = $false,
  [switch]$SkipTests = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "=== Containerized Build & Push ==="
Write-Host "Tag: $Tag"

# Validate scripts
$repoRoot = Get-Location
$winScript = Join-Path $repoRoot 'scripts\windows_build.ps1'
$wslScript = Join-Path $repoRoot 'scripts\build.sh'
$dockerScript = Join-Path $repoRoot 'scripts\docker_build_local.sh'

foreach ($s in @($winScript, $wslScript, $dockerScript)) {
  if (-not (Test-Path $s)) { Write-Warning "Expected script missing: $s" }
}

# Check Docker
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) { throw "docker not found in PATH" }

# Run Windows build
if (-not $SkipWindowsBuild) {
  if ($UseWindowsContainer) {
    if (-not $WinBuilderImage) { throw "WinBuilderImage must be set when UseWindowsContainer is requested (set WIN_BUILDER_IMAGE env or pass -WinBuilderImage)" }
    Write-Host "Running Windows build inside container image: $WinBuilderImage"

    # Ensure Docker is in windows container mode (best effort check)
    $info = docker info --format '{{json .}}' 2>$null | ConvertFrom-Json
    if ($info -and $info.OSType -ne 'windows') {
      Write-Warning "Docker reports OSType=$($info.OSType). To run Windows containers, Docker must be switched to Windows container mode. Attempting to run the container anyway may fail."
    }

    # Map repo to container path C:\src\themis
    $hostRepo = (Get-Location).Path
    $containerRepo = 'C:\src\themis'

    # Build commands to run inside container
    $innerCommand = "powershell -NoProfile -ExecutionPolicy Bypass -Command `"Set-Location $containerRepo; .\\scripts\\windows_build.ps1 $($SkipTests ? '-RunTests:$false' : '')`""

    Write-Host "docker run --rm -v '$hostRepo':'$containerRepo' $WinBuilderImage $innerCommand"
    # Run container
    & docker run --rm -v "${hostRepo}:${containerRepo}" --workdir "${containerRepo}" $WinBuilderImage powershell -NoProfile -ExecutionPolicy Bypass -Command "Set-Location $containerRepo; .\scripts\windows_build.ps1 $($SkipTests ? '-RunTests:$false' : '')"
  } else {
    Write-Host "Running Windows build locally via $winScript"
    $wbArgs = @()
    if ($SkipTests) { $wbArgs += '-RunTests:$false' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $winScript @wbArgs
  }
} else {
  Write-Host "Skipping Windows build as requested"
}

# Run WSL build
if (-not $SkipWSLBuild) {
  Write-Host "Running WSL build via $wslScript"
  # Convert Windows repo path to WSL path via wslpath -u when available
  $winRepo = (Get-Location).Path
  $wslRepo = $null
  try {
    $wslRepo = (& wsl wslpath -u "$winRepo").Trim()
    if (-not $wslRepo) { throw 'empty' }
  } catch {
    Write-Warning "Could not convert path via 'wsl wslpath -u'. Falling back to heuristic /mnt/<drive>/ conversion. Error: $_"
    $drive = $winRepo.Substring(0,1).ToLower()
    $rest = $winRepo.Substring(2) -replace '\\','/' -replace ':',''
    $wslRepo = "/mnt/$drive/$rest"
  }

  # Create artifacts logs dir to store resolution info
  $artifactsLogs = Join-Path $repoRoot 'artifacts\logs'
  New-Item -ItemType Directory -Force -Path $artifactsLogs | Out-Null
  $wslPathLog = Join-Path $artifactsLogs 'wsl_path_resolution.log'
  "Resolution attempts for Windows path: $winRepo" | Out-File -FilePath $wslPathLog -Encoding utf8

  function Test-WslDirExists($path) {
    try { & wsl test -d "$path" ; return ($LASTEXITCODE -eq 0) } catch { return $false }
  }

  $candidate = $null
  try { $candidate = (& wsl wslpath -u "$winRepo" 2>$null).Trim() } catch { $null = $false }
  if ($candidate) { "wslpath -u => $candidate" | Out-File -FilePath $wslPathLog -Append -Encoding utf8 }
  if ($candidate -and (Test-WslDirExists $candidate)) {
    $wslRepo = $candidate
    "Selected candidate: $candidate" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
  } else {
    # Fallback variants
    $drive = $winRepo.Substring(0,1).ToLower()
    $rest = $winRepo.Substring(2) -replace '\\','/' -replace ':',''
    $variants = @("/mnt/$drive/$rest", "/mnt/$($drive.ToUpper())/$rest", "/$drive/$rest")
    foreach ($v in $variants) {
      "Testing variant: $v" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
      if (Test-WslDirExists $v) {
        $wslRepo = $v
        "Selected variant: $v" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
        break
      } else {
        "Variant not found: $v" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
      }
    }
  }

  if (-not $wslRepo) {
    $err = "Could not resolve a valid WSL path for $winRepo. See $wslPathLog for attempts."
    Write-Error $err
    throw $err
  }

  # Run wsl build with explicit triplet and build dir name
  $buildDirName = "build-$LinuxTriplet"
  $envArgs = "VCPKG_TARGET_TRIPLET=$LinuxTriplet BUILD_DIR=$buildDirName"
  $wslCmd = "cd $wslRepo && $envArgs bash ./scripts/build.sh"
  if ($SkipTests) { $wslCmd += ' RUN_TESTS=0' }
  & wsl bash -lc "$wslCmd"
} else {
  Write-Host "Skipping WSL build as requested"
}

# If both builds succeed, build docker image and optionally push
if ($PushDocker) {
  if (-not $DockerTag) { $DockerTag = "themis:$Tag" }
  Write-Host "Building Docker image with tag $DockerTag"

  $noCacheFlag = ''
  if ($NoDockerCache) { $noCacheFlag = '--no-cache' }

  # Call the local docker build script inside bash (it is a bash script)
  & bash -lc "./scripts/docker_build_local.sh --tag $DockerTag $($NoDockerCache ? '--no-cache' : '') --push"
} else {
  Write-Host "PushDocker not requested; build step for docker can be run manually using scripts/docker_build_local.sh"
}

Write-Host "Containerized build & push finished"

exit 0
