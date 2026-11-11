# Direct MSVC host build; does not invoke make or WSL. Run from any directory.
$ErrorActionPreference = 'Stop'
$pagerRoot = Split-Path $PSScriptRoot -Parent
Push-Location $pagerRoot
$pagerSavedInclude = $env:INCLUDE
$pagerSavedLib = $env:LIB
try {
    $pagerVswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $pagerVs = & $pagerVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$pagerVs) { throw 'MSVC C++ Build Tools not found' }
    $pagerVersion = (Get-Content "$pagerVs/VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt").Trim()
    $pagerVc = "$pagerVs/VC/Tools/MSVC/$pagerVersion"
    $pagerSdk = "${env:ProgramFiles(x86)}/Windows Kits/10"
    $pagerSdkVersion = (Get-ChildItem "$pagerSdk/Include" -Directory | Sort-Object Name -Descending | Select-Object -First 1).Name
    $env:INCLUDE = "$pagerVc/include;$pagerSdk/Include/$pagerSdkVersion/ucrt;$pagerSdk/Include/$pagerSdkVersion/shared;$pagerSdk/Include/$pagerSdkVersion/um"
    $env:LIB = "$pagerVc/lib/x64;$pagerSdk/Lib/$pagerSdkVersion/ucrt/x64;$pagerSdk/Lib/$pagerSdkVersion/um/x64"
    New-Item -ItemType Directory -Force tests/build | Out-Null
    # Existing ActionQueue.cpp uses GCC's printf format attribute. Erase that
    # annotation for this MSVC host build only, without changing plugin code.
    Set-Content tests/build/gcc-attributes.hpp '#define __attribute__(x)'
    $pagerSources = @('tests/ride_pager_test.cpp', 'Sources/Screens/RidePager.cpp',
        'Sources/Screens/NativeScreen.cpp', 'Sources/ActionQueue.cpp', 'Sources/Settings.cpp',
        'Sources/SettingsFile.cpp', 'Sources/RomfsText.cpp', 'Sources/PartyData.cpp')
    $pagerSources += @('GameAssets','Theme','Raster','Text','Font','Layer','Icons','Glyphs') | ForEach-Object { "Sources/UI/$_.cpp" }
    & "$pagerVc/bin/Hostx64/x64/cl.exe" /nologo /std:c++17 /EHsc /W4 /D_CRT_SECURE_NO_WARNINGS /I Includes /I . /FI tests/build/gcc-attributes.hpp /Fo:tests/build/ /Fe:tests/build/ride_pager_test.exe $pagerSources
    if ($LASTEXITCODE -ne 0) { throw 'Ride Pager host compilation failed' }
    & ./tests/build/ride_pager_test.exe tests/build/ride-pager-preview.ppm
    if ($LASTEXITCODE -ne 0) { throw 'Ride Pager host test failed' }
} finally {
    $env:INCLUDE = $pagerSavedInclude
    $env:LIB = $pagerSavedLib
    Pop-Location
}
