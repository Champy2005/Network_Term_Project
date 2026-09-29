# macOS setup

Use Terminal with the Apple Clang toolchain. Run project commands from the
repository root, where `CMakePresets.json` and `vcpkg.json` live. Obtain the
project from your teammate's shared Git remote or archive.

## 1. Install the tools

Install Apple's Command Line Tools if they are not already installed:

```sh
xcode-select --install
```

Complete the macOS installer dialog. If the tools are already installed, this
command reports that fact. Verify with `xcode-select -p` and
`clang++ --version`. Full Xcode is also suitable when its command line tools
are selected.

Install [Homebrew](https://docs.brew.sh/Installation) if needed and follow its
post-install instructions to put `brew` on `PATH`. Then install CMake, Ninja,
`pkg-config`, and common vcpkg port build tools:

```sh
brew install cmake ninja pkgconf autoconf autoconf-archive automake libtool
```

The project needs **CMake 3.24 or newer**. Check the prerequisite commands:

```sh
cmake --version
ninja --version
git --version
clang++ --version
pkg-config --version
command -v tar zip unzip curl
```

If one of `tar`, `zip`, `unzip`, or `curl` is missing, install it before running
vcpkg. macOS typically provides these utilities; vcpkg requires them.

## 2. Install vcpkg once

If you already have vcpkg, set `VCPKG_ROOT` to its existing path and skip the
clone and bootstrap commands. For a new installation:

```sh
mkdir -p "$HOME/src"
git clone https://github.com/microsoft/vcpkg.git "$HOME/src/vcpkg"
export VCPKG_ROOT="$HOME/src/vcpkg"
cd "$VCPKG_ROOT"
./bootstrap-vcpkg.sh
test -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" && echo 'vcpkg ready'
```

Export `VCPKG_ROOT` again in every new Terminal session, or add the export line
to your shell startup file (usually `~/.zshrc`). The repository's manifest pins
the dependency baseline. CMake invokes vcpkg during configuration, so no
separate `vcpkg install` command is needed.

## 3. Configure, build, and test

Change back to your project checkout, then run:

```sh
cd /path/to/final_project # replace with your checkout path
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The first configure downloads and builds vcpkg dependencies and may take a
while. Success means `ctest` reports **100% tests passed**. There is currently
one `chat_tests` executable for the framing and Protobuf unit cases. Artifacts
are in `build/debug`.

The `release` preset follows the same command sequence. The optional `asan`
preset enables AddressSanitizer and UndefinedBehaviorSanitizer for project
targets when supported by the local compiler. Use `debug` for the required
first build.

## If setup fails

| Symptom | Check |
| --- | --- |
| `xcrun` or compiler tools cannot find an active developer directory | Complete the Command Line Tools installation, then check `xcode-select -p`. |
| `brew`, `cmake`, or `ninja` is not found | Finish Homebrew's shell setup; open a new Terminal and repeat the version checks. |
| CMake is older than 3.24 | Update the installed CMake and verify `cmake --version` before configuring. |
| `Could not find toolchain file` | Re-export the correct `VCPKG_ROOT`; verify the `test -f` check above succeeds. |
| CMake keeps an old compiler or vcpkg path | Run `cmake --preset debug --fresh`, then build again. |

This checkpoint tests the protocol foundation. There is no runnable chat
server or client yet. See the [project overview](../../README.md) and
[architecture](../architecture.md).

## Tool references

[Apple Command Line Tools](https://developer.apple.com/documentation/xcode/installing-the-command-line-tools),
[Homebrew installation](https://docs.brew.sh/Installation),
[Homebrew CMake](https://formulae.brew.sh/formula/cmake),
[vcpkg host requirements](https://learn.microsoft.com/en-us/vcpkg/concepts/supported-hosts),
[vcpkg manifest with CMake](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode).
