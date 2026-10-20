# Editors and source documentation

Open the **repository folder**, not just one `.cpp` file. This project now
shares one clangd configuration between Zed and VS Code, and has a separate
documentation environment. Application builds still use CMake/vcpkg.

## What each tool does

| Tool | Purpose | Configuration |
| --- | --- | --- |
| CMake + vcpkg | Build targets and discover dependency/compiler headers | `CMakeLists.txt`, presets, `vcpkg.json` |
| clangd | C++ completion, diagnostics, hover, references, and navigation | `.clangd` + `build/debug/compile_commands.json` |
| clang-format | Format C++ consistently; clangd includes this engine | `.clang-format` |
| clang-tidy | Catch correctness/performance issues while editing | `.clang-tidy` |
| Doxygen | Read C++ documentation comments; generate XML and standalone HTML | `docs/Doxyfile` |
| Sphinx | Build the guide and API website | `docs/sphinx/` |
| Breathe | Insert Doxygen XML into Sphinx pages | `breathe` in `conf.py` |
| MyST Parser | Let Sphinx read the existing Markdown guides | `myst_parser` in `conf.py` |
| Mermaid extension | Render the architecture diagram in generated HTML | `sphinxcontrib.mermaid` |

The original missing-header errors occurred because clangd searched above
the source files, while CMake wrote its database in `build/debug`. `.clangd`
now points there explicitly. That database supplies C++20, project/generated
include directories, vcpkg headers, and target definitions. Avoid maintaining
a second manual list of include paths in editor settings.

The workspace also replaces this machine's old VS Code argument pointing at
`build` rather than `build/debug`. `.clang-tidy` retains analyzer, bug-prone,
and performance checks. Three noisy suggestions about string-parameter pairs,
test switch defaults, and public enum size are excluded explicitly. Intentional
discard of shutdown error-code returns must use `static_cast<void>`.

## First setup on Windows

The [Windows build guide](windows.md) covers the Visual Studio C++ workload.
From a regular PowerShell terminal in this repository:

```powershell
# Install these only if absent. This machine already has clangd and Doxygen.
winget install --id LLVM.clangd --exact
winget install --id DimitriVanHeesch.Doxygen --exact

# Discover Visual Studio, load x64 tools, configure Debug, and generate chat.pb.h.
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev.ps1 -Action prepare

# Check all handwritten C++ sources and headers without opening an editor.
python tools/check_clangd.py

# Python 3.11+; installs packages only into .venv-docs.
python tools/docs.py setup
python tools/docs.py build
python tools/docs.py serve
```

The preview address is
[http://127.0.0.1:8000/docs/sphinx/index.html](http://127.0.0.1:8000/docs/sphinx/index.html).
Keep the serve terminal running; Ctrl+C stops it. Serving binds to loopback
only. To choose another port, use `python tools/docs.py serve --port 8001`.
The build creates `build/docs/html/docs/sphinx/index.html` and standalone
Doxygen HTML in `build/docs/doxygen/html/index.html`. These are disposable
outputs; edit comments and Markdown rather than generated HTML.

`dev.ps1` locates Visual Studio using `vswhere`. It honors an existing
`VCPKG_ROOT`; otherwise it uses Visual Studio's bundled vcpkg when available.
No personal installation paths are committed. You can also run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev.ps1 -Action test -Preset debug
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev.ps1 -Action test -Preset release
powershell -NoProfile -ExecutionPolicy Bypass -File tools/dev.ps1 -Action test -Preset asan
```

If Python is installed as `py` rather than `python`, substitute `py -3` for
the Python commands. Doxygen is installed separately from Python. If its
executable has a custom location, set `DOXYGEN_EXECUTABLE` to its full path
before building docs. A new terminal may be needed after installing tools.

On macOS/Linux, use the corresponding build guide to set `VCPKG_ROOT` and
configure/build Debug. Install clangd and Doxygen with your platform package
manager. The shared `.clangd`, Python scripts, and documentation configuration
work with native paths; the Windows bootstrap tasks are Windows-only. If your
Python launcher is `python3`, use that in commands/tasks. Native builds on
those platforms remain unverified.

## VS Code

The repository provides `.vscode/settings.json`, `extensions.json`, and
`tasks.json`; these are **workspace** settings and preserve your global
settings. Install the recommended clangd and CMake Tools extensions if absent.
Doxygen Documentation Generator (`cschlosser.doxdocgen`) is a convenience for
typing comment skeletons; it is not required by the documentation build.

clangd owns C++ diagnostics/formatting in this workspace. Microsoft's C/C++
IntelliSense/formatter is disabled here to avoid two competing services; its
debugger can still be used. The existing Code Runner extension's single-file
compile action does not know this project's linked targets: use CMake tasks.

1. Run **Tasks: Run Task → C++: prepare clangd (Debug)** once.
2. Run **clangd: Restart language server** after the first build.
3. Open a C++ file; Ctrl+click/F12 should navigate into project/dependency headers.
4. Use **Docs: setup**, **Docs: build**, and **Docs: serve** for documentation.

Markdown preview (`Ctrl+Shift+V`), local-link completion, and link validation
are built into VS Code. This workspace enables link warnings. Path Intellisense
and Markdown All in One are optional conveniences. Live Server is unnecessary:
the supplied docs server already previews the built site. The built-in Markdown
preview does not execute Breathe directives; check the Sphinx HTML for the
complete API reference and Mermaid diagram.

## Zed

Zed supports C++/clangd natively. `.zed/settings.json` sets clangd arguments
and uses the existing four-space formatting style; `.zed/tasks.json` provides
Windows build preparation and portable Python documentation tasks.

Run **task: spawn → Windows: prepare clangd**, then restart the language server
from Zed's command palette. The same root `.clangd` selects the compiler database.
No separate C++ include-path list is needed. Preview Markdown in Zed for prose;
use the generated Sphinx site for API pages and complete documentation rendering.

Both editors normally find the installed `clangd` on PATH. If VS Code cannot,
set `clangd.path` to its executable in your **user** settings. In Zed, set
`lsp.clangd.binary.path` to an absolute executable path in user settings.
Keep those machine-specific paths out of shared configuration.

## Writing useful comments

Every handwritten C++ source/header now has a short `@file` explanation;
each of the 202 test cases has a scenario comment. Public headers document
ownership, ordering, cancellation, deadlines, and acceptance semantics.
Implementation comments explain decisions that are not obvious from names.
The source catalogue also includes scripts, CMake, Protobuf, and shared editor
configuration. Generated/dependency code is excluded.

Use Doxygen comments for API contracts:

```cpp
/// @brief Encode one bounded payload with a big-endian length header.
/// @param payload Serialized bytes; must contain between 1 and 1 MiB.
/// @return Header followed by the unchanged payload.
/// @throws FrameError If the payload length violates the wire bounds.
std::vector<std::uint8_t> encode_frame(std::span<const std::uint8_t> payload);
```

Use `//` for a local explanation of **why**, not a narration of each statement.
For tests, describe the scenario and the property being checked, especially
negative delivery and cancellation. Python uses module/function docstrings;
PowerShell uses comment-based help; Protobuf and CMake use ordinary comments.
JSON cannot contain comments, so its roles are explained here and in the source
catalogue. Document APIs where declared, rather than copying the same contract
into every implementation.

Write architecture/decisions/user instructions in Markdown. Use relative links
such as `[architecture](../architecture.md)` so both editors and the built site
can resolve them. A change to behavior should update the API comment, tests,
architecture, and progress evidence. A change to an implementation detail usually
needs only a nearby comment. Avoid promises about unimplemented future behavior.

`docs/Doxyfile` extracts private symbols for browsing but does not require prose
for every private field. Documentation syntax warnings still fail the build.
Sphinx also builds with warnings treated as errors; Breathe and local Markdown
link errors are caught during that build. The source catalogue is generated
automatically, so new source files appear without writing another index page.

## Diagnosing remaining editor errors

| Symptom | Fix |
| --- | --- |
| Project header missing | Open the repository root; check `.clangd` and regenerate Debug. |
| `chat.pb.h` missing | Build Debug; configure alone does not run protoc. |
| Boost/GoogleTest/Protobuf missing | Configure with the correct vcpkg toolchain; rebuild before restarting clangd. |
| `<vector>` or Windows SDK headers missing | Install the Visual Studio C++ workload/SDK; inspect `build/tooling/clangd/*.log`. Avoid mixing MinGW headers into an MSVC build. |
| Errors after moving repo or updating Visual Studio | Run preparation again; compiler-database paths are machine-specific generated data. |
| Duplicate diagnostics | Verify the workspace C/C++ IntelliSense setting is disabled and restart clangd. |
| Missing database | Confirm `build/debug/compile_commands.json` exists; keep editor indexing on Debug even when testing Release/ASAN. |
| Docs packages missing | Run `python tools/docs.py setup`; activation of the environment is unnecessary. |
| Doxygen missing | Install it or set `DOXYGEN_EXECUTABLE`; the build prints a clear error. |
| Port 8000 in use | Serve with `--port 8001`. |

The automated clangd command checks parsing for all owned `.cpp`/`.hpp` files
and writes per-file logs. It disables clangd's unrelated refactoring-location
self-tests; editor navigation and formatting walkthroughs remain manual checks.
Native Windows message/control-ID pointer casts have narrow, explained
`NOLINTNEXTLINE(performance-no-int-to-ptr)` annotations; the check stays enabled
for other code. Windows checks all 52 owned C++ files; other platforms skip the
four Windows-only GUI files. GUI run tasks are also provided in both editors.

## Official references

- [clangd configuration](https://clangd.llvm.org/config.html)
- [clangd compiler commands](https://clangd.llvm.org/design/compile-commands)
- [Zed C++ configuration](https://zed.dev/docs/languages/cpp)
- [VS Code Markdown support](https://code.visualstudio.com/docs/languages/markdown)
- [Doxygen installation](https://www.doxygen.nl/manual/install.html)
- [Breathe directives and configuration](https://breathe.readthedocs.io/en/latest/directives.html)
- [Sphinx Markdown support](https://www.sphinx-doc.org/en/master/usage/markdown.html)
