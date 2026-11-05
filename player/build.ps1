# Builds the USUM One Screen app and puts what a player needs in dist\:
# UsumOneScreen.exe (plugin embedded), usum-onescreen.3gx (offline zip compatibility)
# and README.txt, optionally with an unmodified Azahar distribution.
param([string]$AzaharDir, [string]$AzaharLicense)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
dotnet build "$here\UsumOneScreen.csproj" -c Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$plugin = Join-Path $here '..\usum-onescreen.3gx'
if (-not (Test-Path $plugin)) { throw "No usum-onescreen.3gx in the repo root; run make first." }
$dist = Join-Path $here 'dist'
New-Item -ItemType Directory -Force $dist | Out-Null
Copy-Item "$here\bin\Release\net48\UsumOneScreen.exe" $dist -Force
Copy-Item $plugin $dist -Force
Copy-Item "$here\PLAYER-README.txt" (Join-Path $dist 'README.txt') -Force

$bundled = Join-Path $dist 'azahar-bundled'
$sourceNotice = Join-Path $dist 'AZAHAR-SOURCE.txt'
if ($AzaharDir) {
    $AzaharDir = (Resolve-Path -LiteralPath $AzaharDir).Path
    if (-not (Test-Path -LiteralPath (Join-Path $AzaharDir 'azahar.exe'))) {
        throw 'AzaharDir must contain azahar.exe.'
    }
    if ($AzaharDir.StartsWith([IO.Path]::GetFullPath($dist), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'AzaharDir must be outside dist.'
    }
    if (Test-Path -LiteralPath (Join-Path $AzaharDir 'user')) {
        throw 'Use a clean Azahar distribution without a user folder (no game files, saves or keys).'
    }
    $licenses = @(Get-ChildItem -LiteralPath $AzaharDir -File -Recurse |
        Where-Object { $_.Name -eq 'COPYING' -or $_.Name -like 'license*' })
    if ($AzaharLicense) {
        if (-not (Test-Path -LiteralPath $AzaharLicense -PathType Leaf)) {
            throw 'AzaharLicense must name a licence file.'
        }
        $AzaharLicense = (Resolve-Path -LiteralPath $AzaharLicense).Path
    }
    if ($licenses.Count -eq 0 -and -not $AzaharLicense) {
        throw 'No COPYING or license* file found in AzaharDir; supply -AzaharLicense FILE.'
    }
}
# Remove only the previous bundle inside this build's dist directory.
if (Test-Path -LiteralPath $bundled) {
    $target = (Resolve-Path -LiteralPath $bundled).Path
    if ($target -ne [IO.Path]::GetFullPath((Join-Path $here 'dist\azahar-bundled'))) {
        throw 'Unexpected bundle destination.'
    }
    Remove-Item -LiteralPath $target -Recurse -Force
}
if (Test-Path -LiteralPath $sourceNotice) { Remove-Item -LiteralPath $sourceNotice -Force }
if ($AzaharDir) {
    Copy-Item -LiteralPath $AzaharDir -Destination $bundled -Recurse
    # Keep the original notices in the bundle and expose the licence at zip root too.
    if ($licenses.Count -gt 0) {
        $license = $licenses | Sort-Object { $_.FullName.Length } | Select-Object -First 1
        Copy-Item -LiteralPath $license.FullName -Destination $dist -Force
    }
    if ($AzaharLicense) {
        Copy-Item -LiteralPath $AzaharLicense -Destination (Join-Path $bundled 'LICENSE-azahar.txt') -Force
        Copy-Item -LiteralPath $AzaharLicense -Destination (Join-Path $dist 'LICENSE-azahar.txt') -Force
    }
    @'
Azahar 2126.2, GPL
Source: https://github.com/azahar-emu/azahar/releases/tag/2126.2
Archive: azahar-unified-source-2126.2.tar.xz
SHA-256: 85c8b56fac703df04720125044b8ff887997911858a6cde545dfb1107ba4cd26
'@ | Set-Content -LiteralPath $sourceNotice -Encoding UTF8
}
Get-ChildItem $dist
