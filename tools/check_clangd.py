"""Check every owned C++ source/header using the same config as both editors."""

import argparse
from pathlib import Path
import shutil
import subprocess
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    """Return failure for missing build inputs or any clangd parsing diagnostic."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--clangd", default="clangd", help="Executable name or full path")
    args = parser.parse_args()
    executable = shutil.which(args.clangd)
    if not executable:
        parser.error("clangd is missing; install it and add it to PATH")
    for required in ("compile_commands.json", "generated/chat.pb.h"):
        if not (ROOT / "build/debug" / required).is_file():
            parser.error("configure and build Debug first; see docs/setup/tooling.md")
    output = ROOT / "build/tooling/clangd"
    output.mkdir(parents=True, exist_ok=True)
    failures = 0
    files = sorted(
        path for folder in ("include", "src", "apps", "tests")
        for path in (ROOT / folder).rglob("*") if path.suffix in (".cpp", ".hpp")
    )
    if sys.platform != "win32":
        # Native controls are only built on Windows; the worker/model are portable.
        files = [path for path in files if path.relative_to(ROOT).as_posix()
                 not in ("apps/gui/main.cpp", "apps/gui/window.cpp", "apps/gui/window.hpp",
                         "tests/integration/gui_test.cpp")]
    for path in files:
        # Location checks exercise refactoring code actions, not C++ diagnostics.
        # Disable them so --check tests parsing rather than unrelated tweak bugs.
        result = subprocess.run(
            [executable, f"--check={path}", "--check-locations=false", "--check-warnings",
             "--log=info"], cwd=ROOT, capture_output=True, text=True, timeout=120,
        )
        relative = path.relative_to(ROOT).as_posix()
        log = result.stdout + result.stderr
        (output / (relative.replace("/", "_") + ".log")).write_text(log, encoding="utf-8")
        # --check-warnings prints warnings but clangd's exit code counts errors
        # only. Treat either as failure so PASS never hides compiler/tidy output.
        diagnostics = re.search(r"^E\[.*\] \[[\w-]+\]", log, flags=re.MULTILINE)
        if result.returncode or diagnostics:
            failures += 1
            print(f"FAIL {relative}\n{log}")
        else:
            print(f"PASS {relative}")
    print(f"{len(files) - failures}/{len(files)} files passed; logs: {output}")
    return int(failures != 0)


if __name__ == "__main__":
    raise SystemExit(main())
