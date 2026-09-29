# Socket chat term project

A C++20 foundation for the computer-network chat assignment. The current
checkpoint is **Stages 0-1**: CMake/vcpkg setup, a Protocol Buffers schema,
four-byte length framing, and automated tests. There is no runnable server or
client yet, so this repository does not yet demonstrate the assignment's chat
requirements. See [architecture and protocol](docs/architecture.md) for the
wire format, intended runtime design, and staged plan. The
[requirements checklist](docs/requirements.md) maps the assignment's graded
behaviors to later implementation and demo evidence.

## Build and test

Requirements: CMake 3.24 or newer, Ninja, a C++20 compiler, and a local
[vcpkg](https://github.com/microsoft/vcpkg) installation. The manifest installs
Protobuf and GoogleTest for Stage 1, plus Boost.Asio, OpenSSL, and SQLite for
later stages. On Windows, open an **x64 Visual Studio Developer
PowerShell** (or Developer Command Prompt) so Ninja can find MSVC, then run
these commands from the repository root:

```powershell
$env:VCPKG_ROOT = 'C:\vcpkg' # replace with your local vcpkg directory
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The `debug`, `release`, and `asan` preset names each work with `cmake --preset`,
`cmake --build --preset`, and `ctest --preset`. The `asan` preset enables
AddressSanitizer with MSVC, or AddressSanitizer and UndefinedBehaviorSanitizer
with GCC/Clang where supported. On Windows, run `ctest --preset asan` in the
Visual Studio developer environment so the MSVC sanitizer runtime is on
`PATH`. Build output is under `build/<preset>`.

The Stage 1 tests cover big-endian encoding, partial headers and payloads,
multiple frames per read, empty and oversized frame rejection, incomplete
frames at EOF, and Protobuf round-trip and malformed-payload parsing. Passing
them verifies the framing and schema foundation, not network behavior.

## Repository layout

| Path | Purpose |
| --- | --- |
| `proto/chat.proto` | v1 typed `Envelope` and chat payload definitions. |
| `shared/include/chat/framing.hpp`, `shared/src/framing.cpp` | Standalone frame encoder and incremental decoder. |
| `tests/` | GoogleTest coverage for framing and Protobuf parsing. |
| `server/`, `client/`, `cli/` | Reserved source boundaries for later checkpoints; no executables exist yet. |
| `docs/architecture.md` | Current status, protocol rules, and later-stage design. |
| `docs/requirements.md` | Assignment criteria, planned stages, and demo evidence. |
| `certs/README.md` | Certificate handling plan for the later TLS stage. |

The frame format is a four-byte unsigned big-endian payload length followed
by a serialized `chat.protocol.Envelope`. The allowed payload size is 1 to
1,048,576 bytes. Frame decoding and Protobuf parsing are separate steps.

## Next implementation checkpoint

Stage 2 will add a separate plaintext TCP server and CLI client and prove
framed `Ping`/`Pong` across a socket. The later stages add session management,
login, private and group rooms, presence, history, reconnect, and TLS. The
assignment ultimately requires at least two clients on different physical
computers, plus a GUI with a message display and text input per room. An AI
feature is also required for the assignment's special-feature section.
