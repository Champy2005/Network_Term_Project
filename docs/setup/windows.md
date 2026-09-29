# Windows setup

This guide builds the repository with MSVC in **PowerShell**. Run commands from
the repository root, the folder containing `CMakePresets.json` and `vcpkg.json`.
If a teammate gives you an archive, extract it first; if they give you a Git
remote, clone it first.

## 1. Install the tools

- Install [Git for Windows](https://git-scm.com/download/win).
- Install Visual Studio or Visual Studio Build Tools with **Desktop development
  with C++**, the x64 MSVC toolset, and a Windows SDK. The Visual Studio CMake
  tools component can provide CMake and Ninja; standalone installations are
  fine too. The project requires **CMake 3.24 or newer** and Ninja.
- Open a **Developer PowerShell for Visual Studio** configured for x64. The
  Visual Studio version in the shortcut name can differ by computer. An x64
  Native Tools Command Prompt also works, but the commands below use PowerShell
  syntax.

Verify the shell before configuring:

```powershell
git --version
cmake --version
ninja --version
cl
```

`cl` may print a missing-source-file warning; its version banner is the useful
part. If a command is unavailable, fix the installation or launch the Visual
Studio developer shell again before continuing.

## 2. Install vcpkg once

If vcpkg is already installed, set `VCPKG_ROOT` to that existing directory and
skip the clone and bootstrap commands. Otherwise:

```powershell
$env:VCPKG_ROOT = Join-Path $HOME 'src\vcpkg'
New-Item -ItemType Directory -Force -Path (Split-Path $env:VCPKG_ROOT) | Out-Null
git clone https://github.com/microsoft/vcpkg.git $env:VCPKG_ROOT
Push-Location $env:VCPKG_ROOT
.\bootstrap-vcpkg.bat
Pop-Location
```

`VCPKG_ROOT` is set only for this shell. Set it again in each new developer
shell, or add it to your user environment. Verify the toolchain file:

```powershell
Test-Path (Join-Path $env:VCPKG_ROOT 'scripts\buildsystems\vcpkg.cmake')
```

This must print `True`. The repository's `vcpkg.json` records its dependency
baseline. CMake uses the vcpkg toolchain from `VCPKG_ROOT` and installs the
manifest dependencies during configuration; a separate `vcpkg install` is not
needed.

## 3. Configure, build, and test

In the same developer shell, change to your copy of the project and run:

```powershell
Set-Location 'C:\path\to\final_project' # replace with your checkout path
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The first configure can take a while because vcpkg builds dependencies. A
successful test run reports **100% tests passed**. `ctest` currently sees one
`chat_tests` executable containing the framing and Protobuf unit cases. Build
output stays in `build/debug`.

For a release build, run the same three commands with `release`. The optional
`asan` preset enables MSVC AddressSanitizer; run its tests from the same Visual
Studio developer shell so the sanitizer runtime is available on `PATH`.

## If setup fails

| Symptom | Check |
| --- | --- |
| `cl` or `ninja` is not found | Reopen the x64 Visual Studio developer shell. Check the C++ workload and CMake/Ninja installation. |
| CMake is older than 3.24 | Install or select a newer CMake, then confirm `cmake --version` in this shell. |
| `Could not find toolchain file` | Confirm `VCPKG_ROOT` and that the `Test-Path` command above prints `True`. |
| Configuration still uses an old compiler or vcpkg path | In the correct developer shell, run `cmake --preset debug --fresh`, then build again. |
| `ctest --preset asan` cannot load the sanitizer runtime | Run it from the Visual Studio developer shell used for the build. |

This checkpoint has protocol and framing tests. It does not yet have a runnable
chat server or client. See the [project overview](../../README.md) and
[architecture](../architecture.md) for what is implemented and planned.

## Tool references

[MSVC installation](https://learn.microsoft.com/en-us/cpp/overview/acquire-msvc),
[x64 developer shell](https://learn.microsoft.com/en-us/cpp/build/how-to-enable-a-64-bit-visual-cpp-toolset-on-the-command-line),
[vcpkg with CMake](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started-vs),
[CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).
