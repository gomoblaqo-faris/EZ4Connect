# Windows deployment script
# Usage: .\deploy-windows.ps1 -TargetName "EZ4Connect" -DisplayName "EZ4Connect" -BuildDir "build" -Architecture "amd64" -Nightly "false"

param(
    [string]$TargetName = "EZ4Connect",
    [string]$DisplayName = "EZ4Connect",
    [string]$BuildDir = "build",
    [string]$Architecture = "amd64",
    [string]$Nightly = "false"
)

Import-Module -Name Microsoft.PowerShell.Utility

# Stop at the first failing step instead of packaging whatever is left.
$ErrorActionPreference = "Stop"

# Create output directory
New-Item -ItemType Directory -Path "$DisplayName" -Force
Push-Location "$DisplayName"

# Copy executable
Copy-Item -Path "../$BuildDir/Release/$TargetName.exe" -Destination .

# Run windeployqt
& windeployqt.exe "$TargetName.exe"

# Download and extract zju-connect
$ZjuZipPath = "zju-connect-windows-$Architecture.zip"
$ZjuReleases = "https://github.com/Mythologyli/zju-connect/releases"
if ($Nightly -eq "true") {
    # A nightly build is replaced under the same name, so it cannot be pinned.
    Write-Warning "Bundling an unverified nightly zju-connect"
    Invoke-WebRequest -Uri "$ZjuReleases/download/nightly/$ZjuZipPath" -OutFile $ZjuZipPath
} else {
    $PinnedRelease = Join-Path $PSScriptRoot "zju-connect-release.txt"
    $ZjuVersion = $null
    $ZjuSha256 = $null
    foreach ($Line in Get-Content $PinnedRelease) {
        $Fields = $Line.Trim() -split '\s+'
        if ($Fields.Count -ne 2) { continue }
        if ($Fields[0] -eq "version") { $ZjuVersion = $Fields[1] }
        if ($Fields[0] -eq $ZjuZipPath) { $ZjuSha256 = $Fields[1] }
    }
    if (-not $ZjuVersion -or -not $ZjuSha256) {
        throw "No pinned version or checksum for $ZjuZipPath in $PinnedRelease"
    }
    Invoke-WebRequest -Uri "$ZjuReleases/download/$ZjuVersion/$ZjuZipPath" -OutFile $ZjuZipPath
    $ActualSha256 = (Get-FileHash -Path $ZjuZipPath -Algorithm SHA256).Hash
    if ($ActualSha256 -ne $ZjuSha256) {
        throw "Checksum mismatch for ${ZjuZipPath}: expected $ZjuSha256, got $ActualSha256"
    }
}
Expand-Archive -Path $ZjuZipPath -DestinationPath . -Force
Remove-Item -Path $ZjuZipPath

# Copy additional files
Copy-Item -Path "../libs/wintun/bin/$Architecture/wintun.dll" -Destination .
Copy-Item -Path "../resource/qt.conf" -Destination .

# Remove vc_redist executable
if ($Architecture -eq "amd64") {
    Remove-Item -Path vc_redist.x64.exe -ErrorAction SilentlyContinue
} else {
    Remove-Item -Path vc_redist.arm64.exe -ErrorAction SilentlyContinue
}

Pop-Location
