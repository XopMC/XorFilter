# Author: Mikhail Khoroshavin aka "XopMC"
param(
    [string]$Exe = ".\x64\Release\hex_to_xor.exe"
)

$ErrorActionPreference = "Stop"

$fixture = "tests\fixtures\keys_small.txt"
$generatedDir = "tests\generated"
$goldenDir = "tests\golden"
$expectedFiles = @(
    "keys_small_0.xor_u",
    "keys_small_0.xor_c",
    "keys_small_0.xor_uc",
    "keys_small_0.xor_hc"
)

# Keep the generated directory clean so the comparison is deterministic.
New-Item -ItemType Directory -Force $generatedDir | Out-Null
Remove-Item -Path (Join-Path $generatedDir "*") -Force -ErrorAction SilentlyContinue

& $Exe -i $fixture -o $generatedDir
& $Exe -i $fixture -compress -o $generatedDir
& $Exe -i $fixture -ultra -o $generatedDir
& $Exe -i $fixture -hyper -o $generatedDir

foreach ($name in $expectedFiles) {
    $generatedPath = Join-Path $generatedDir $name
    $goldenPath = Join-Path $goldenDir $name

    if (-not (Test-Path $goldenPath)) {
        throw "Missing golden file: $goldenPath"
    }
    if (-not (Test-Path $generatedPath)) {
        throw "Missing generated file: $generatedPath"
    }

    $generatedBytes = [System.IO.File]::ReadAllBytes($generatedPath)
    $goldenBytes = [System.IO.File]::ReadAllBytes($goldenPath)
    if ($generatedBytes.Length -ne $goldenBytes.Length -or
        -not [System.Linq.Enumerable]::SequenceEqual($generatedBytes, $goldenBytes)) {
        throw "Byte mismatch for $name"
    }
}

if (Get-Command clang-cl -ErrorAction SilentlyContinue) {
    clang-cl /std:c++20 /EHsc /nologo /I. tests\filter_compat_tests.cpp /Fe:tests\filter_compat_tests.exe | Out-Null
    & .\tests\filter_compat_tests.exe
}

Write-Host "Smoke tests passed"
