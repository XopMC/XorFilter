<#
hex_to_xor
Author: Mikhail Khoroshavin aka "XopMC"
Packages the Windows build into a release-ready zip with docs, manifest, and SHA256.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinaryPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$SourceSha
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$null = New-Item -ItemType Directory -Force $OutputDir
$packageRoot = Join-Path $OutputDir "XorFilter-$Version-windows-x64"
$archivePath = Join-Path $OutputDir "XorFilter-$Version-windows-x64.zip"
$shaPath = "$archivePath.sha256"

New-Item -ItemType Directory -Force $packageRoot | Out-Null
Copy-Item -Force (Join-Path $repoRoot $BinaryPath) (Join-Path $packageRoot "hex_to_xor.exe")
Copy-Item -Force (Join-Path $repoRoot "README.md") (Join-Path $packageRoot "README.md")
Copy-Item -Force (Join-Path $repoRoot "LICENSE.txt") (Join-Path $packageRoot "LICENSE.txt")

$manifest = @"
Project: hex_to_xor
Version: $Version
Platform: windows-x64
Source commit: $SourceSha
Packaged at: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
"@
Set-Content -Path (Join-Path $packageRoot "MANIFEST.txt") -Value $manifest -Encoding ASCII

if (Test-Path $archivePath) {
    Remove-Item -Force $archivePath
}
Compress-Archive -Path $packageRoot -DestinationPath $archivePath -Force

$hash = (Get-FileHash -Algorithm SHA256 -Path $archivePath).Hash.ToLowerInvariant()
Set-Content -Path $shaPath -Value "$hash  $(Split-Path -Leaf $archivePath)" -Encoding ASCII
Remove-Item -Recurse -Force $packageRoot

Write-Host "Created $archivePath"
Write-Host "Created $shaPath"
