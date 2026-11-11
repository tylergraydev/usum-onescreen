$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location $repo
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$env:Path += ';C:\Program Files (x86)\Microsoft Visual Studio\Installer'
$environment = & cmd.exe /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'vcvars64 failed' }
foreach ($line in $environment) {
    if ($line -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process') }
}
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$ui = @('Raster','Text','Font','Layer','Icons','Glyphs','Demo','GameAssets','Theme','ThemeDemo') | ForEach-Object { "Sources/UI/$_.cpp" }
$common = @('Sources/Settings.cpp','Sources/SettingsFile.cpp','Sources/RomfsText.cpp','Sources/PartyData.cpp','Sources/ActionQueue.cpp') + $ui
$flags = @('/nologo','/std:c++17','/EHsc','/W3','/D_CRT_SECURE_NO_WARNINGS','/IIncludes',"/FI$PSScriptRoot/msvc_host_compat.hpp",'/wd4244',"/Fo$out\")
& cl.exe @flags tests/pelago_test.cpp Sources/Screens/Pelago.cpp Sources/Screens/NativeScreen.cpp @common "/Fe$out/pelago_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'pelago compilation failed' }
& "$out/pelago_test.exe" $out
if ($LASTEXITCODE -ne 0) { throw 'pelago tests failed' }
& cl.exe @flags tests/native_screens_test.cpp Sources/Screens/NativeScreen.cpp Sources/Screens/Options.cpp Sources/Screens/TrainerPassport.cpp Sources/Screens/BattleVideo.cpp @common "/Fe$out/native_screens_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'native screens compilation failed' }
& "$out/native_screens_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'native screens tests failed' }
& cl.exe @flags tests/action_queue_test.cpp Sources/ActionQueue.cpp "/Fe$out/action_queue_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'action queue compilation failed' }
& "$out/action_queue_test.exe"
if ($LASTEXITCODE -ne 0) { throw 'action queue tests failed' }
& python tools/gen_layouts.py --check
if ($LASTEXITCODE -ne 0) { throw 'layout header mismatch' }
