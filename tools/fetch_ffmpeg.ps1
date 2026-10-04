<#
.SYNOPSIS
    Fetches a prebuilt, NVENC-capable FFmpeg (shared libs + headers) for CamBot, if it
    isn't already present or a newer build has been published.

.DESCRIPTION
    CamBot used to depend on a hand-built FFmpeg (built via MSYS2), which was gnarly to
    set up and had no GPU encoding support. This script replaces that with a prebuilt
    Windows release from BtbN/FFmpeg-Builds (the same community builds linked from
    ffmpeg.org's own downloads page) -- GPL, shared-linking, win64, n8.1 branch. It's
    wired into CamBot.vcxproj as a pre-build step, so cloning the repo and building it
    "just works" without ever touching MSYS2.

    This uses BtbN's "latest" release and its stable, version-string-free asset names
    (release tag literally named "latest", e.g. ffmpeg-n8.1-latest-win64-gpl-shared-8.1.zip)
    rather than pinning one dated build -- on request, since staying current with as little
    manual upkeep as possible mattered more here than pinning an exact build. It's still
    safe to run on every build: checksums.sha256 (a few KB) is fetched fresh every time and
    compared against what's already unpacked, and the full .zip is only re-downloaded when
    that hash actually changes (i.e. BtbN has published a newer n8.1 build).

    To switch to a different FFmpeg branch/variant later, change $AssetName below --
    nothing else needs to know its version, since "latest" always points at whatever
    BtbN currently publishes under that name.
#>

$ErrorActionPreference = "Stop"

# BtbN's "latest" release keeps a stable, version-string-free name for exactly this kind
# of use. This is the n8.1 (stable) branch, GPL, dynamically-linked, win64 build.
$AssetName      = "ffmpeg-n8.1-latest-win64-gpl-shared-8.1.zip"
$ChecksumsAsset = "checksums.sha256"
$BaseUrl        = "https://github.com/BtbN/FFmpeg-Builds/releases/latest/download"

$RepoRoot   = Split-Path -Parent $PSScriptRoot
$FfmpegDir  = Join-Path $RepoRoot "third_party\ffmpeg"
$MarkerFile = Join-Path $FfmpegDir ".fetched"

# Older PowerShell/.NET defaults to TLS 1.0, which GitHub's download/release hosts reject.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# Plain System.Net.WebClient instead of Invoke-WebRequest: WebClient is just .NET (always
# available, nothing to resolve), whereas Invoke-WebRequest comes from the
# Microsoft.PowerShell.Utility module, which on at least one machine this was tried on
# didn't load the way it was expected to.
function Get-RemoteFile([string]$Uri, [string]$OutFile) {
    $client = New-Object System.Net.WebClient
    try {
        $client.DownloadFile($Uri, $OutFile)
    } finally {
        $client.Dispose()
    }
}

function Get-RemoteString([string]$Uri) {
    $client = New-Object System.Net.WebClient
    try {
        return $client.DownloadString($Uri)
    } finally {
        $client.Dispose()
    }
}

Write-Host "fetch_ffmpeg: checking for the latest $AssetName ..."
$checksumsText = Get-RemoteString -Uri "$BaseUrl/$ChecksumsAsset"
$checksumLine = ($checksumsText -split "`r?`n") | Where-Object { $_ -match [regex]::Escape($AssetName) }
if (-not $checksumLine) {
    throw "Could not find a checksums.sha256 entry for $AssetName -- has BtbN renamed this asset?"
}
$expectedHash = ($checksumLine -split '\s+')[0].ToLowerInvariant()

function Test-AlreadyFetched {
    if (-not (Test-Path $MarkerFile)) { return $false }
    $marker = Get-Content -Raw $MarkerFile -ErrorAction SilentlyContinue
    if ($marker -ne $expectedHash) { return $false }
    foreach ($sub in @("include", "lib", "bin")) {
        if (-not (Test-Path (Join-Path $FfmpegDir $sub))) { return $false }
    }
    return $true
}

if (Test-AlreadyFetched) {
    Write-Host "fetch_ffmpeg: third_party\ffmpeg is already up to date -- nothing to do."
    exit 0
}

Write-Host "fetch_ffmpeg: setting up FFmpeg into third_party\ffmpeg (this is a one-time download, ~90MB) ..."

$WorkDir   = Join-Path $env:TEMP ("cambot_ffmpeg_" + [System.Guid]::NewGuid().ToString("N"))
$ZipPath   = Join-Path $WorkDir $AssetName

New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null

try {
    Write-Host "fetch_ffmpeg: downloading $AssetName ..."
    Get-RemoteFile -Uri "$BaseUrl/$AssetName" -OutFile $ZipPath

    Write-Host "fetch_ffmpeg: verifying sha256 ..."
    $actualHash = (Get-FileHash -Path $ZipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "sha256 mismatch for $AssetName (expected $expectedHash, got $actualHash) -- download may be corrupt or tampered with."
    }

    Write-Host "fetch_ffmpeg: extracting ..."
    $ExtractDir = Join-Path $WorkDir "extracted"
    Expand-Archive -Path $ZipPath -DestinationPath $ExtractDir -Force

    # The zip contains a single top-level folder (named after the build, e.g.
    # "ffmpeg-n8.1-latest-win64-gpl-shared-8.1\"); flatten its include/lib/bin straight into
    # third_party\ffmpeg so project paths don't need to know that name.
    $innerDir = Get-ChildItem -Path $ExtractDir -Directory | Select-Object -First 1
    if (-not $innerDir) {
        throw "Unexpected archive layout: no top-level folder found in $AssetName"
    }

    if (Test-Path $FfmpegDir) {
        Remove-Item -Path $FfmpegDir -Recurse -Force
    }
    New-Item -ItemType Directory -Path $FfmpegDir -Force | Out-Null

    foreach ($sub in @("include", "lib", "bin")) {
        $src = Join-Path $innerDir.FullName $sub
        if (-not (Test-Path $src)) {
            throw "Expected '$sub' directory not found in extracted release ($src)"
        }
        Copy-Item -Path $src -Destination (Join-Path $FfmpegDir $sub) -Recurse -Force
    }

    Set-Content -Path $MarkerFile -Value $expectedHash -NoNewline

    Write-Host "fetch_ffmpeg: done -- third_party\ffmpeg is ready."
}
finally {
    Remove-Item -Path $WorkDir -Recurse -Force -ErrorAction SilentlyContinue
}
