"""Set up, build, or locally serve the Doxygen/Sphinx/Breathe documentation."""

import argparse
import functools
import http.server
import os
from pathlib import Path
import shutil
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/docs"
ENV = ROOT / ".venv-docs"


def environment_python() -> Path:
    """Locate the documentation virtual environment on either supported OS layout."""
    return ENV / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def run(command: list[str]) -> None:
    """Run a tool from the repository root and propagate build failures."""
    subprocess.run(command, cwd=ROOT, check=True)


def find_doxygen() -> str:
    """Accept a user override, PATH installation, or Windows installer location."""
    executable = shutil.which(os.environ.get("DOXYGEN_EXECUTABLE", "doxygen"))
    if executable:
        return executable
    if os.name == "nt":
        candidate = Path(os.environ.get("ProgramFiles", "C:/Program Files")) / "doxygen/bin/doxygen.exe"
        if candidate.is_file():
            return str(candidate)
    raise RuntimeError("Doxygen is missing; see docs/setup/tooling.md for installation commands")


def owned_code() -> list[Path]:
    """Catalogue handwritten code/config only, excluding dependencies and build output."""
    files = []
    extensions = {".cpp", ".hpp", ".proto", ".py", ".ps1", ".cmake", ".json"}
    for folder in ("include", "src", "apps", "tests", "proto", "tools", ".vscode", ".zed"):
        files.extend(path for path in (ROOT / folder).rglob("*") if path.suffix in extensions)
    files.extend(ROOT / name for name in (
        "CMakeLists.txt", "CMakePresets.json", "vcpkg.json", ".clangd", ".clang-format", ".clang-tidy",
        ".gitignore", "docs/Doxyfile", "docs/sphinx/conf.py", "docs/requirements-docs.txt",
    ))
    return sorted(files)


def stage_source() -> Path:
    """Copy documentation inputs preserving relative Markdown links and source paths."""
    source = BUILD / "source"
    # Only replace this disposable build subtree; never remove repository sources.
    if not source.resolve().is_relative_to(ROOT.resolve()):
        raise RuntimeError("unexpected documentation staging directory")
    if source.exists():
        shutil.rmtree(source)
    files = [ROOT / "README.md", ROOT / "AGENTS.md", ROOT / "certs/README.md"]
    files += [path for path in (ROOT / "docs").rglob("*") if path.is_file()]
    files += owned_code()
    for path in files:
        target = source / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    reference = source / "reference"
    reference.mkdir()
    entries = []
    languages = {".cpp": "cpp", ".hpp": "cpp", ".proto": "protobuf", ".py": "python",
                 ".ps1": "powershell", ".json": "json"}
    for number, path in enumerate(owned_code()):
        relative = path.relative_to(ROOT).as_posix()
        title = relative
        language = languages.get(path.suffix, "text")
        if path.name == "CMakeLists.txt":
            language = "cmake"
        name = f"file-{number:03d}"
        (reference / f"{name}.rst").write_text(
            f"{title}\n{'=' * len(title)}\n\n"
            f".. literalinclude:: /{relative}\n   :language: {language}\n   :linenos:\n",
            encoding="utf-8",
        )
        entries.append(f"   {name}")
    (reference / "index.rst").write_text(
        "Source catalogue\n================\n\n"
        "Every handwritten source, test, schema, script, and shared configuration is listed here.\n"
        "Generated Protobuf and dependency sources are excluded.\n\n"
        ".. toctree::\n   :maxdepth: 1\n\n" + "\n".join(entries) + "\n", encoding="utf-8",
    )
    return source


def build() -> None:
    """Fail on documentation warnings and generate both reference and guide HTML."""
    python = environment_python()
    if not python.is_file():
        raise RuntimeError("Run python tools/docs.py setup first")
    doxygen = find_doxygen()
    BUILD.mkdir(parents=True, exist_ok=True)
    run([doxygen, "docs/Doxyfile"])
    source = stage_source()
    environment = os.environ.copy()
    environment["CHAT_DOXYGEN_XML"] = str(BUILD / "doxygen/xml")
    subprocess.run(
        [str(python), "-m", "sphinx", "-b", "html", "-W", "--keep-going", "-E",
         "-c", str(source / "docs/sphinx"), str(source), str(BUILD / "html")],
        cwd=ROOT, env=environment, check=True,
    )
    print(f"Documentation: {BUILD / 'html/docs/sphinx/index.html'}")


def main() -> int:
    """Dispatch isolated installation, strict HTML build, or loopback-only preview."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("setup", "build", "serve"))
    parser.add_argument("--port", type=int, default=8000)
    args = parser.parse_args()
    try:
        if args.action == "setup":
            venv.EnvBuilder(with_pip=True).create(ENV)
            run([str(environment_python()), "-m", "pip", "install", "-r", "docs/requirements-docs.txt"])
            print("Python documentation tools installed. Doxygen is installed separately.")
        elif args.action == "build":
            build()
        else:
            if not (BUILD / "html/docs/sphinx/index.html").is_file():
                raise RuntimeError("Run python tools/docs.py build first")
            handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(BUILD / "html"))
            with http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler) as server:
                print(f"Preview: http://127.0.0.1:{server.server_port}/docs/sphinx/index.html", flush=True)
                server.serve_forever()
    except KeyboardInterrupt:
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError) as error:
        print(f"Documentation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
