<#
.SYNOPSIS
Prepare the Windows C++ toolchain and run a project preset or clangd check.
.DESCRIPTION
Discovers Visual Studio with vswhere so editor tasks work without machine paths.
#>
param(
    [ValidateSet('prepare', 'build', 'test', 'check')][string]$Action = 'prepare',
    [ValidateSet('debug', 'release', 'asan')][string]$Preset = 'debug',
    [switch]$VerboseTests
)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path -LiteralPath $taskVswhere)) {
    throw 'Install Visual Studio with Desktop development with C++ (see docs/setup/windows.md).'
}
$taskVs = & $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $taskVs) { throw 'No Visual Studio C++ toolchain found.' }
& (Join-Path $taskVs 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if (-not $env:VCPKG_ROOT -or $env:VCPKG_ROOT -eq (Join-Path $taskVs 'VC\vcpkg')) {
    $taskBundledVcpkg = if (Test-Path -LiteralPath (Join-Path $HOME 'src/vcpkg/scripts/buildsystems/vcpkg.cmake')) { Join-Path $HOME 'src/vcpkg' } else { Join-Path $taskVs 'VC/vcpkg' }
    if (Test-Path -LiteralPath "$taskBundledVcpkg/scripts/buildsystems/vcpkg.cmake") {
        $env:VCPKG_ROOT = $taskBundledVcpkg
    } else { throw 'Set VCPKG_ROOT to your vcpkg checkout (see docs/setup/windows.md).' }
}
$taskCmakeBin = Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
$taskNinjaBin = Join-Path $taskVs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
$taskCleanPath = ($env:PATH -split ';' | Where-Object { $_ -and ($_ -notmatch 'msys64') }) -join ';'
$env:PATH = "$taskCmakeBin;$taskNinjaBin;$taskCleanPath"
Push-Location $taskRoot
try {
    & cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    # A configure produces the database; a build also produces chat.pb.h.
    & cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }
    if ($Action -eq 'test') {
        $taskTestArguments = @('--preset', $Preset, '--no-tests=error')
        if ($VerboseTests) { $taskTestArguments += '--verbose' }
        & ctest @taskTestArguments
        if ($LASTEXITCODE -ne 0) { throw 'CTest failed.' }
    }
    if ($Action -eq 'check') {
        if ($Preset -ne 'debug') { throw 'clangd uses Debug; run -Action check -Preset debug.' }
        & python tools/check_clangd.py
        if ($LASTEXITCODE -ne 0) { throw 'clangd check failed.' }
    }
} finally { Pop-Location }
