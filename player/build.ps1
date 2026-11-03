# Builds the USUM One Screen app and puts what a player needs in dist\:
# UsumOneScreen.exe, usum-onescreen.3gx (from the repo root, built with make)
# and README.txt. No Azahar, game files or keys go in.
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
Get-ChildItem $dist
