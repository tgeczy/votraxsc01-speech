# license:BSD-3-Clause
# build.ps1 -- builds votraxsc01-nvda with bare MSVC Build Tools, no solution
# files.  Modeled on panthera-speech's build script.
#
#   .\build.ps1                # probe tool + x64 core DLL
#   .\build.ps1 -Target probe  # just tools/say01.exe
#   .\build.ps1 -Target dll    # x64 + x86 sc01.dll (NVDA and SAPI cores)
#
# Everything is /MT (static CRT): NVDA does not ship the VC++ redist, and a
# DLL that works on the dev machine but not a clean one is the bug this
# repo's author has already been bitten by.  dumpbin /dependents is run on
# every built DLL to prove it.
param(
    [string]$Target = "all",
    [string]$OutDir = "$PSScriptRoot\build"
)
$ErrorActionPreference = "Stop"

$msvc = Get-ChildItem "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC" -Directory |
        Sort-Object Name | Select-Object -Last 1
$sdk = Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" -Directory |
       Sort-Object Name | Select-Object -Last 1
if (!$msvc -or !$sdk) { throw "MSVC Build Tools 2022 and a Windows 10/11 SDK are required" }
$sdkLib = Join-Path (Split-Path (Split-Path $sdk.FullName)) "Lib\$($sdk.Name)"

$inc = @(
    "/I$PSScriptRoot\src\shim",
    "/I$PSScriptRoot\src\core",
    "/I$PSScriptRoot\src\frontend",
    "/I$PSScriptRoot\third_party\mame",
    "/I$($msvc.FullName)\include",
    "/I$($sdk.FullName)\ucrt",
    "/I$($sdk.FullName)\um",
    "/I$($sdk.FullName)\shared"
)

function Invoke-CL([string]$Arch, [string[]]$Flags) {
    $cl = Join-Path $msvc.FullName "bin\Hostx64\$Arch\cl.exe"
    $lib = @("/LIBPATH:$($msvc.FullName)\lib\$Arch",
             "/LIBPATH:$sdkLib\ucrt\$Arch",
             "/LIBPATH:$sdkLib\um\$Arch")
    & $cl @Flags @lib
    if ($LASTEXITCODE) { throw "cl.exe ($Arch) failed with $LASTEXITCODE" }
}

function Assert-StaticCRT([string]$Binary) {
    $dumpbin = Join-Path $msvc.FullName "bin\Hostx64\x64\dumpbin.exe"
    $deps = & $dumpbin /dependents $Binary | Out-String
    if ($deps -match "VCRUNTIME|MSVCP|api-ms-win-crt") {
        throw "$Binary depends on the VC++ runtime -- the static-CRT rule is broken:`n$deps"
    }
}

# The four rules files from the USENIX 1987 tape compile byte-identical;
# only parse.c was replaced (by wasser_parse.c) -- see THIRD_PARTY_LICENSES.
$core = @("$PSScriptRoot\src\core\sc01.cpp",
          "$PSScriptRoot\src\core\timestretch.c",
          "$PSScriptRoot\src\frontend\text_to_votrax.c",
          "$PSScriptRoot\src\frontend\wasser_parse.c",
          "$PSScriptRoot\src\frontend\arpabet_to_sc01.c",
          "$PSScriptRoot\src\frontend\exceptions.c",
          "$PSScriptRoot\third_party\wasser\english.c",
          "$PSScriptRoot\third_party\wasser\phoneme.c",
          "$PSScriptRoot\third_party\wasser\saynum.c",
          "$PSScriptRoot\third_party\wasser\spellword.c",
          "$PSScriptRoot\third_party\mame\votrax.cpp")
# /wd4244 and /wd4805 mirror MAME's own build settings for its sources
# (bitswap narrowing into u8 registers is idiomatic there, not a bug).
$cxx = @("/nologo", "/std:c++20", "/EHsc", "/O2", "/MT", "/W3", "/DUNICODE", "/D_UNICODE",
         "/D_CRT_SECURE_NO_WARNINGS", "/wd4244", "/wd4805")

New-Item -ItemType Directory -Force $OutDir, "$OutDir\x64", "$OutDir\x86", "$OutDir\obj" | Out-Null

if ($Target -in "probe", "all") {
    Invoke-CL x64 ($cxx + $inc + @("$PSScriptRoot\tools\say01.c") + $core +
        @("/Fe$OutDir\say01.exe", "/Fo$OutDir\obj\", "/link"))
    Assert-StaticCRT "$OutDir\say01.exe"
    Write-Host "built $OutDir\say01.exe"
}

if ($Target -in "dll", "all") {
    foreach ($arch in "x64", "x86") {
        Invoke-CL $arch ($cxx + @("/LD", "/DVX_BUILD_DLL") + $inc + $core +
            @("/Fe$OutDir\$arch\sc01.dll", "/Fo$OutDir\obj\", "/link"))
        Assert-StaticCRT "$OutDir\$arch\sc01.dll"
        Write-Host "built $OutDir\$arch\sc01.dll"
    }
}

if ($Target -in "test", "all") {
    # The frontend gate runs everywhere (no ROM needed); the chip gate is
    # real where a ROM exists and announces its skip where none does.
    $py = "C:\Python313\python.exe"
    if (Test-Path $py) {
        & $py "$PSScriptRoot\tests\frontend_test.py"
        if ($LASTEXITCODE) { throw "frontend golden tests failed" }
        & $py "$PSScriptRoot\tests\stretch_test.py"
        if ($LASTEXITCODE) { throw "stretch tests failed" }
        & $py "$PSScriptRoot\tests\chip_test.py"
        if ($LASTEXITCODE) { throw "chip tests failed" }
    } else {
        Write-Host "tests skipped: no Python at $py"
    }
}

if ($Target -in "sapi", "all") {
    # The SAPI engine links the chip statically: one DLL per architecture,
    # no dependency chain.  Both architectures matter -- 32-bit hosts
    # (JAWS, older SAPI apps) load the x86 voice, 64-bit hosts the x64.
    foreach ($arch in "x64", "x86") {
        Invoke-CL $arch ($cxx + @("/LD") + $inc +
            @("$PSScriptRoot\sapi\votrax_sapi.cpp") + $core +
            @("/Fe$OutDir\$arch\votrax_sapi.dll", "/Fo$OutDir\obj\", "/link",
              "/DEF:$PSScriptRoot\sapi\votrax_sapi.def",
              "sapi.lib", "ole32.lib", "advapi32.lib", "user32.lib"))
        Assert-StaticCRT "$OutDir\$arch\votrax_sapi.dll"
        Write-Host "built $OutDir\$arch\votrax_sapi.dll"
        Get-ChildItem "$PSScriptRoot\roms\*.bin" -ErrorAction SilentlyContinue |
            Copy-Item -Destination "$OutDir\$arch\"
    }
}

if ($Target -in "addon", "all") {
    # Stage the NVDA add-on: driver sources + both core DLLs, zipped with
    # the .nvda-addon extension.  ROMs are the user's to add.
    $stage = "$OutDir\addon-stage"
    if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
    Copy-Item -Recurse "$PSScriptRoot\nvda-addon" $stage
    Copy-Item "$OutDir\x64\sc01.dll" "$stage\synthDrivers\votraxsc01\sc01-x64.dll"
    Copy-Item "$OutDir\x86\sc01.dll" "$stage\synthDrivers\votraxsc01\sc01-x86.dll"
    # Release policy (the PC-ROBOT/BraiLab convention): the repo never
    # carries the ROM dumps, but a release build on a machine that has them
    # in roms\ ships them inside the bundle, under MAME's licensing note.
    Get-ChildItem "$PSScriptRoot\roms\*.bin" -ErrorAction SilentlyContinue |
        Copy-Item -Destination "$stage\synthDrivers\votraxsc01\"
    $manifest = Get-Content "$stage\manifest.ini" | Where-Object { $_ -match '^version = (.+)$' }
    $version = $Matches[1]
    $bundle = "$OutDir\votraxsc01-$version.nvda-addon"
    if (Test-Path $bundle) { Remove-Item $bundle }
    Compress-Archive -Path "$stage\*" -DestinationPath "$bundle.zip"
    Move-Item "$bundle.zip" $bundle
    Write-Host "built $bundle"
}
