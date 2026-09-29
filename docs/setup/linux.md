# Linux setup

Use a native Linux terminal with a C++20 compiler. The commands below cover
Debian and Ubuntu; package names may differ on other distributions. Run the
project commands from the repository root, where `CMakePresets.json` and
`vcpkg.json` live. Obtain the project from your teammate's shared Git remote
or archive.

## 1. Install the tools

On Debian or Ubuntu:

```sh
sudo apt update
sudo apt install build-essential cmake ninja-build git curl zip unzip tar \
  pkg-config autoconf autoconf-archive automake libtool
```

On Fedora, equivalent packages can be installed with:

```sh
sudo dnf install gcc-c++ make cmake ninja-build git curl zip unzip tar \
  pkgconf-pkg-config autoconf autoconf-archive automake libtool
```

Confirm that **CMake is at least 3.24**, Ninja is available, and your compiler
supports C++20:

```sh
cmake --version
ninja --version
git --version
g++ --version
pkg-config --version
```

If your distribution's CMake is too old, install a newer release from
[Kitware's official packages](https://apt.kitware.com/) or another official
CMake distribution and verify the version again. The vcpkg host requirements
include archive tools, curl, Git, and `pkg-config`; the other development tools
support ports built from source.

## 2. Install vcpkg once

If vcpkg is already installed, set `VCPKG_ROOT` to that directory and skip the
clone and bootstrap commands. Otherwise:

```sh
mkdir -p "$HOME/src"
git clone https://github.com/microsoft/vcpkg.git "$HOME/src/vcpkg"
export VCPKG_ROOT="$HOME/src/vcpkg"
cd "$VCPKG_ROOT"
./bootstrap-vcpkg.sh
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" && echo 'vcpkg ready'
```

Export `VCPKG_ROOT` in every new shell, or put the export line in your shell
startup file. The repository's `vcpkg.json` pins its dependency baseline, and
the CMake configure step installs the manifest dependencies automatically.

## 3. Configure, build, and test

Change back to your project checkout, then run:

```sh
cd /path/to/final_project # replace with your checkout path
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The first configure can be slow while vcpkg builds dependencies. Success means
`ctest` reports **100% tests passed**. There is currently one `chat_tests`
executable for the framing and Protobuf unit cases. Output is in `build/debug`.

Repeat the command sequence with `release` for an optimized build. The
optional `asan` preset enables AddressSanitizer and UndefinedBehaviorSanitizer
with GCC or Clang; use `debug` for the required first build.

## If setup fails

| Symptom | Check |
| --- | --- |
| CMake rejects the preset version | Check `cmake --version`; this project needs 3.24 or newer. |
| `ninja` or `pkg-config` is not found | Install the distro's Ninja and pkg-config packages, then repeat the version checks. |
| `Could not find toolchain file` | Re-export `VCPKG_ROOT` and verify the `test -f` check above succeeds. |
| vcpkg says a host build tool is missing | Install the tool named in its error; check the distro package names against the vcpkg host requirements. |
| CMake keeps an old compiler or vcpkg path | Run `cmake --preset debug --fresh`, then build again. |

This checkpoint only tests the protocol foundation. There is no runnable chat
server or client yet. See the [project overview](../../README.md) and
[architecture](../architecture.md).

## Tool references

[vcpkg host requirements](https://learn.microsoft.com/en-us/vcpkg/concepts/supported-hosts),
[vcpkg manifest with CMake](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode),
[Kitware Ubuntu packages](https://apt.kitware.com/),
[CMake presets](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html).
