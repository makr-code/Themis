<#
Orchestrator to perform a full release build locally (Windows host)
- Runs WSL build (Linux) via WSL installed bash script
- Runs Windows build via scripts/windows_build.ps1
- Collects artifacts into artifacts/release/<tag>/
- Optionally creates a git commit and pushes (supports GIT_TOKEN env if you want non-interactive push)
- Optionally builds and pushes Docker image(s) using scripts/docker_build_local.sh

Usage (PowerShell, run from repo root on Windows):
  .\scripts\release_build.ps1 -Tag 1.2.3 -DockerTag myrepo/themis:1.2.3 -PushGit -PushDocker

Parameters:
  -Tag <string>          : Release tag or identifier (default: timestamp)
  -RunWSL               : Switch (default: $true) to run WSL build via wsl bash
  -RunWindows           : Switch (default: $true) to run local Windows build script
  -PushGit              : Switch to create git commit and push artifacts
  -GitMessage <string>  : Git commit message (default: "release <tag>")
  -UseGitToken          : Use GIT_TOKEN env var to push (insecure; token must have repo scope)
  -PushDocker           : Switch to build & push Docker image
  -DockerTag <string>   : Docker tag to use (default: ghcr style omitted; default themis:<tag>)
  -NoDockerCache        : Pass --no-cache to docker build
  -SkipTests            : Skip running tests in sub-builds
  -ArtifactsDir <path>  : base artifacts directory (default: artifacts/release)

Security notes:
- If you use GIT_TOKEN it will be used in a temporary remote URL. Avoid logging tokens.
- Docker push requires you to be logged in (docker login) or to set DOCKER_USER/DOCKER_PASSWORD env vars.
#>

param(
    [string]$Tag = $(Get-Date -Format 'yyyyMMdd-HHmmss'),
    [string]$LinuxTriplet = 'x64-linux',
    [switch]$RunWSL = $true,
    [switch]$RunWindows = $true,
    [switch]$PushGit = $false,
    [string]$GitMessage = '',
    [switch]$UseGitToken = $false,
    [switch]$PushDocker = $false,
    [string]$DockerTag = '',
    [switch]$NoDockerCache = $false,
    [switch]$SkipTests = $false,
    [string]$ArtifactsDir = 'artifacts\release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

Write-Host "=== Themis Full Release Orchestrator ==="
Write-Host "Tag: $Tag"

$repoRoot = Get-Location
$releaseDir = Join-Path $repoRoot (Join-Path $ArtifactsDir $Tag)
Write-Host "Release artifacts dir: $releaseDir"
New-Item -ItemType Directory -Force -Path $releaseDir | Out-Null

# Helper: gather metadata (git commit, cmake versions, vcpkg commits)
function Get-GitCommit() {
    try { return (& git rev-parse --short HEAD).Trim() } catch { return $null }
}

function Get-CMakeVersionWindows() {
    try { $out = (& cmake --version 2>$null)[0]; return $out } catch { return $null }
}

function Get-CMakeVersionWsl() {
    try { $out = (& wsl cmake --version 2>$null) -join "`n"; if ($out) { return $out.Split("`n")[0] } else { return $null } } catch { return $null }
}

function Get-VcpkgCommitWindows() {
    # Try VCPKG_ROOT env or ./vcpkg
    $candidates = @()
    if ($env:VCPKG_ROOT) { $candidates += $env:VCPKG_ROOT }
    $candidates += Join-Path $repoRoot 'vcpkg'
    foreach ($p in $candidates) {
        if (Test-Path (Join-Path $p '.git')) {
            try { return (& git -C $p rev-parse --short HEAD).Trim() } catch { }
        }
    }
    return $null
}

function Get-VcpkgCommitWsl($wslRepo) {
    try {
        $cmd = "if [ -d '$wslRepo/vcpkg/.git' ]; then git -C '$wslRepo/vcpkg' rev-parse --short HEAD; fi"
        $out = (& wsl bash -lc $cmd 2>$null).Trim()
        if ($out) { return $out } else { return $null }
    } catch { return $null }
}

$global:BUILD_METADATA = @{}
$global:BUILD_METADATA.git_commit = Get-GitCommit
$global:BUILD_METADATA.build_time_utc = (Get-Date).ToUniversalTime().ToString('o')
$global:BUILD_METADATA.cmake_version_windows = Get-CMakeVersionWindows

# 1) Run WSL build
if ($RunWSL) {
    Write-Host "Starting WSL (Linux) build..."
    # Convert current Windows repo path to WSL path using wslpath -u when available
    $winRepo = (Get-Location).Path
    $wslRepo = $null
    $logsDir = Join-Path $releaseDir 'logs'
    New-Item -ItemType Directory -Force -Path $logsDir | Out-Null
    $wslPathLog = Join-Path $logsDir 'wsl_path_resolution.log'
    "Resolution attempts for Windows path: $winRepo" | Out-File -FilePath $wslPathLog -Encoding utf8
    $attempts = @()
    try {
        $candidate = (& wsl wslpath -u "$winRepo" 2>$null).Trim()
        if ($candidate) { $attempts += "wslpath -u => $candidate" }
    } catch { $attempts += "wslpath -u failed: $_" }

    # helper to test if a wsl path exists
    function Test-WslDirExists($path) {
        try {
            $res = (& wsl test -d "$path"; echo $?).Trim() 2>$null
            return ($LASTEXITCODE -eq 0)
        } catch {
            return $false
        }
    }

    if ($candidate) {
        "Testing candidate: $candidate" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
        if (Test-WslDirExists $candidate) {
            $wslRepo = $candidate
            "Candidate exists: $candidate" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
        } else {
            "Candidate does not exist: $candidate" | Out-File -FilePath $wslPathLog -Append -Encoding utf8
        }
    }

    if (-not $wslRepo) {
        # fallback heuristics: C:\Some\Path -> /mnt/c/Some/Path and try variations
        $drive = $winRepo.Substring(0,1).ToLower()
        $rest = $winRepo.Substring(2) -replace '\\','/' -replace ':',''
        $variants = @()
        $variants += "/mnt/$drive/$rest"
        $variants += "/mnt/$($drive.ToUpper())/$rest"
        # also try without '/mnt' (rare setups)
        $variants += "/$drive/$rest"

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
        $msg = "Failed to resolve a valid WSL path for repo ($winRepo). Attempts logged to $wslPathLog"
        Write-Error $msg
        throw $msg
    }

    # Run build with explicit triplet and build dir per-triplet
    $buildDirName = "build-$LinuxTriplet"
    $envArgs = "VCPKG_TARGET_TRIPLET=$LinuxTriplet BUILD_DIR=$buildDirName"
    $wslCmd = "cd $wslRepo && $envArgs bash ./scripts/build.sh"
    if ($SkipTests) { $wslCmd += ' RUN_TESTS=0' }
    Write-Host "Running: wsl bash -lc '$wslCmd' (resolved path logged to $wslPathLog)"
    $logPath = Join-Path $releaseDir 'wsl_build.log'
    & wsl bash -lc "$wslCmd" 2>&1 | Tee-Object -FilePath $logPath

    # Set linux build path on Windows side
    $linuxBuildPath = Join-Path $repoRoot $buildDirName
    Write-Host "WSL build finished; log at $logPath"
    # Copy Linux artifacts (from Windows-mounted path build/..)
    $linuxBuildPath = Join-Path $repoRoot 'build'
    if (Test-Path $linuxBuildPath) {
        Write-Host "Copying Linux build artifacts from $linuxBuildPath to release dir"
        $linuxDest = Join-Path $releaseDir (Join-Path 'linux' $LinuxTriplet)
        New-Item -ItemType Directory -Force -Path $linuxDest | Out-Null
        # copy typical binaries (no .exe)
        Get-ChildItem -Path $linuxBuildPath -Recurse -Filter 'themis*' -ErrorAction SilentlyContinue | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $linuxDest -Force
        }

        # Gather WSL-specific metadata
        $global:BUILD_METADATA.cmake_version_wsl = Get-CMakeVersionWsl
        $global:BUILD_METADATA.vcpkg_commit_windows = Get-VcpkgCommitWindows
        $global:BUILD_METADATA.vcpkg_commit_wsl = Get-VcpkgCommitWsl $wslRepo

        # Write metadata file into linuxDest
        $meta = [ordered]@{
            tag = $Tag
            platform = 'linux'
            triplet = $LinuxTriplet
            git_commit = $global:BUILD_METADATA.git_commit
            build_time_utc = $global:BUILD_METADATA.build_time_utc
            cmake_version_windows = $global:BUILD_METADATA.cmake_version_windows
            cmake_version_wsl = $global:BUILD_METADATA.cmake_version_wsl
            vcpkg_commit_windows = $global:BUILD_METADATA.vcpkg_commit_windows
            vcpkg_commit_wsl = $global:BUILD_METADATA.vcpkg_commit_wsl
        }
        $metaPath = Join-Path $linuxDest 'metadata.json'
        $meta | ConvertTo-Json -Depth 5 | Out-File -FilePath $metaPath -Encoding utf8

        # Create per-triplet archive for Linux (tar.gz if tar available, otherwise zip)
        $archivesDir = Join-Path $releaseDir 'archives'
        New-Item -ItemType Directory -Force -Path $archivesDir | Out-Null
        $linuxArchiveBase = "themis-$Tag-linux-$LinuxTriplet"
        # prefer tar if available
        $tarAvailable = $false
        try { & tar --version > $null 2>&1; $tarAvailable = $true } catch { $tarAvailable = $false }
        if ($tarAvailable) {
            $tarPath = Join-Path $archivesDir ("$linuxArchiveBase.tar.gz")
            Write-Host "Creating tar.gz archive: $tarPath"
            # use tar to create gz compressed archive
            & tar -C "$linuxDest" -czf "$tarPath" .
        } else {
            $zipPath = Join-Path $archivesDir ("$linuxArchiveBase.zip")
            Write-Host "tar not found; creating zip archive: $zipPath"
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [System.IO.Compression.ZipFile]::CreateFromDirectory($linuxDest, $zipPath)
        }

        # --- Build artifact manifest: collect archives, checksums, sizes, and extra metadata ---
        function Get-FileSha256($path) {
            try { return ((Get-FileHash -Algorithm SHA256 -Path $path).Hash).ToLower() } catch { return $null }
        }

        # compute vcpkg.json hash if present
        $vcpkgJsonPath = Join-Path $repoRoot 'vcpkg.json'
        $vcpkgJsonHash = $null
        if (Test-Path $vcpkgJsonPath) { $vcpkgJsonHash = Get-FileSha256 $vcpkgJsonPath }

        # helper: try to parse selected keys from a CMakeCache.txt file
        function Parse-CMakeCacheKeys($cachePath, [string[]]$keys) {
            $result = @{}
            if (Test-Path $cachePath) {
                $content = Get-Content $cachePath -ErrorAction SilentlyContinue
                foreach ($k in $keys) {
                    $match = $content | Select-String -Pattern "^$k:.*=(.*)$" -SimpleMatch -Quiet:$false
                    if ($match) {
                        $line = ($content | Select-String -Pattern "^$k:.*=(.*)$").Matches[0].Groups[1].Value
                        $result[$k] = $line
                    } else {
                        $result[$k] = $null
                    }
                }
            }
            return $result
        }

        # try to extract CMakeCache info from Linux build dir (if exists)
        $cmakeInfoLinux = @{}
        $linuxCMakeCache = Join-Path $linuxBuildPath 'CMakeCache.txt'
        if (Test-Path $linuxCMakeCache) {
            $cmakeInfoLinux = Parse-CMakeCacheKeys $linuxCMakeCache @('CMAKE_COMMAND','CMAKE_GENERATOR','CMAKE_BUILD_TYPE','CMAKE_TOOLCHAIN_FILE','VCPKG_TARGET_TRIPLET')
        }

        # Build manifest entries for archives
        $manifest = [ordered]@{
            tag = $Tag
            generated_at_utc = (Get-Date).ToUniversalTime().ToString('o')
            git_commit = $global:BUILD_METADATA.git_commit
            vcpkg_json_hash = $vcpkgJsonHash
            archives = @()
        }

        Get-ChildItem -Path $archivesDir -File -ErrorAction SilentlyContinue | ForEach-Object {
            $file = $_
            $sha = Get-FileSha256 $file.FullName
            $entry = [ordered]@{
                filename = $file.Name
                path = $file.FullName
                size = $file.Length
                sha256 = $sha
            }
            $manifest.archives += $entry
        }

        # attach cmakeInfoLinux to manifest if available
        if ($cmakeInfoLinux.Count -gt 0) { $manifest.cmake_linux = $cmakeInfoLinux }

        $manifestPath = Join-Path $releaseDir 'manifest.json'
        $manifest | ConvertTo-Json -Depth 6 | Out-File -FilePath $manifestPath -Encoding utf8
        Write-Host "Wrote manifest to $manifestPath"
    } else {
        Write-Warning "Expected linux build path $linuxBuildPath not found"
    }
}

# 2) Run Windows build
if ($RunWindows) {
    Write-Host "Starting Windows build..."
    $winBuildScript = Join-Path $repoRoot 'scripts\windows_build.ps1'
    if (-not (Test-Path $winBuildScript)) { throw "$winBuildScript not found" }
    $wbArgs = @()
    if ($SkipTests) { $wbArgs += '-RunTests:$false' }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $winBuildScript @wbArgs
    # Collect windows artifacts from artifacts/windows
    $winArtifactsRoot = Join-Path $repoRoot 'artifacts\windows'
    if (Test-Path $winArtifactsRoot) {
        # copy per-triplet/config into release dir
        $tripletDirs = Get-ChildItem -Path $winArtifactsRoot -Directory -ErrorAction SilentlyContinue
        foreach ($t in $tripletDirs) {
            $configDirs = Get-ChildItem -Path $t.FullName -Directory -ErrorAction SilentlyContinue
            foreach ($c in $configDirs) {
                $winDest = Join-Path $releaseDir (Join-Path 'windows' (Join-Path $t.Name $c.Name))
                New-Item -ItemType Directory -Force -Path $winDest | Out-Null
                Write-Host "Copying windows artifacts from $($c.FullName) to $winDest"
                Get-ChildItem -Path $c.FullName -Recurse -File | ForEach-Object {
                    Copy-Item -Path $_.FullName -Destination $winDest -Force
                }
                # Create metadata for this windows triplet/config
                $metaWin = [ordered]@{
                    tag = $Tag
                    platform = 'windows'
                    triplet = $t.Name
                    config = $c.Name
                    git_commit = $global:BUILD_METADATA.git_commit
                    build_time_utc = $global:BUILD_METADATA.build_time_utc
                    cmake_version_windows = $global:BUILD_METADATA.cmake_version_windows
                    vcpkg_commit_windows = $global:BUILD_METADATA.vcpkg_commit_windows
                }
                $metaWinPath = Join-Path $winDest 'metadata.json'
                $metaWin | ConvertTo-Json -Depth 5 | Out-File -FilePath $metaWinPath -Encoding utf8

                # Create per-triplet/config zip archive for Windows
                $archivesDir = Join-Path $releaseDir 'archives'
                New-Item -ItemType Directory -Force -Path $archivesDir | Out-Null
                $winArchiveName = "themis-$Tag-windows-$($t.Name)-$($c.Name).zip"
                $winArchivePath = Join-Path $archivesDir $winArchiveName
                Write-Host "Creating Windows zip archive: $winArchivePath"
                Add-Type -AssemblyName System.IO.Compression.FileSystem
                [System.IO.Compression.ZipFile]::CreateFromDirectory($winDest, $winArchivePath)
            }
        }
    } else {
        Write-Warning "Windows artifacts folder $winArtifactsRoot not found"
    }
}

# 3) Create zip of release (optional)
$zipPath = Join-Path $releaseDir "themis-$Tag.zip"
Write-Host "Creating zip archive: $zipPath"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($releaseDir, $zipPath)

# 4) Git commit & push
if ($PushGit) {
    Write-Host "Preparing git commit and push..."
    $curBranch = (& git rev-parse --abbrev-ref HEAD).Trim()
    $commitMsg = if ($GitMessage) { $GitMessage } else { "release $Tag" }

    # Stage release artifacts
    & git add $releaseDir
    & git commit -m $commitMsg

    if ($UseGitToken) {
        if (-not $env:GIT_TOKEN) { throw "GIT_TOKEN environment variable not set" }
        # get remote URL
        $originUrl = (& git config --get remote.origin.url).Trim()
        if ($originUrl -like 'https://*') {
            # inject token (username can be x-access-token for GH)
            $tokenUrl = $originUrl -replace '^https://', "https://$($env:GIT_TOKEN)@"
            Write-Host "Pushing using token to origin"
            & git push $tokenUrl $curBranch
        } else {
            Write-Host "Origin URL is not HTTPS; attempting normal push"
            & git push origin $curBranch
        }
    } else {
        Write-Host "Pushing via configured credentials (docker credential helper or ssh)"
        & git push origin $curBranch
    }
}

# 5) Docker build & push (per-arch images)
if ($PushDocker) {
    if (-not $DockerTag) { $DockerTag = "themis:$Tag" }
    Write-Host "Building per-arch Docker images with base tag $DockerTag"

    # Build Linux images per triplet
    $linuxRoot = Join-Path $releaseDir 'linux'
    if (Test-Path $linuxRoot) {
        Get-ChildItem -Path $linuxRoot -Directory | ForEach-Object {
            $tripletName = $_.Name
            $imageTag = "$DockerTag-linux-$tripletName"
            Write-Host "Building Linux image for triplet $tripletName -> $imageTag"
            # build using Dockerfile.runtime and pass TRIPLET
            $buildCmd = "docker build -f Dockerfile.runtime --build-arg TRIPLET=$tripletName -t $imageTag '$releaseDir'"
            Write-Host $buildCmd
            iex $buildCmd
            if ($?) {
                Write-Host "Built $imageTag"
                if ($PushDocker) {
                    Write-Host "Pushing $imageTag"
                    docker push $imageTag
                }
            } else {
                Write-Warning "Failed to build $imageTag"
            }
        }
    } else {
        Write-Warning "No linux artifacts found at $linuxRoot. Skipping linux image builds."
    }

    # Note: Windows container images are not auto-built by default. Building Windows images
    # requires Docker in Windows Container mode and a suitable Windows Dockerfile. If you
    # want this, I can add a windows runtime Dockerfile and build step as well.
}

Write-Host "Release orchestration finished. Artifacts at $releaseDir"

exit 0
