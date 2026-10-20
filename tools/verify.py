"""Run scoped acceptance gates with owned-process cleanup and honest evidence."""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = sys.platform == "win32"
FLAGS = subprocess.CREATE_NO_WINDOW if WINDOWS else 0


@dataclass(frozen=True)
class Step:
    """One named command; empty-suite rejection applies only to CTest steps."""
    name: str
    command: tuple[str, ...]
    tests: bool = False


def ps_quote(value: str) -> str:
    """Quote a literal PowerShell argument, including paths with apostrophes."""
    return "'" + value.replace("'", "''") + "'"


def plan(profile: str, skip_asan: str | None = None, windows: bool = WINDOWS) -> list[Step]:
    """Use existing tools rather than a parallel build/test implementation."""
    if profile not in {"workflow", "quick", "full"}:
        raise ValueError("unknown verification profile")
    if skip_asan is not None and (profile != "full" or not skip_asan.strip()):
        raise ValueError("ASAN omission needs the full profile and a nonempty reason")
    python = sys.executable
    steps = [Step("workflow-artifacts", (python, "tools/check_workflow.py")),
             Step("workflow-tests", (python, "-m", "unittest", "discover", "-s", "tools/tests", "-v"), tests=True)]
    if profile == "workflow":
        return steps
    presets = ["debug"] if profile == "quick" else ["debug", "release", "asan"]
    if skip_asan:
        presets.remove("asan")
    for preset in presets:
        if windows:
            # Keep smoke children inside the same VS environment, especially ASAN's DLL PATH.
            code = ("$ErrorActionPreference = 'Stop'; "
                    f"& 'tools/dev.ps1' -Action test -Preset {ps_quote(preset)} -VerboseTests; "
                    "if (-not $?) { exit 1 }; ")
            if profile == "full":
                for helper in ("check_cli_apps.py", "check_tls_apps.py"):
                    code += (f"& {ps_quote(python)} {ps_quote('tools/' + helper)} --preset {ps_quote(preset)}; "
                             "if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ")
            code += "exit 0"
            steps.append(Step(preset, ("powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                       "-Command", code), tests=True))
        else:
            steps.extend((Step(f"{preset}-configure", ("cmake", "--preset", preset)),
                          Step(f"{preset}-build", ("cmake", "--build", "--preset", preset)),
                          Step(f"{preset}-test", ("ctest", "--preset", preset, "--no-tests=error", "--verbose"), tests=True)))
            if profile == "full":
                steps.extend(Step(f"{preset}-{name}", (python, f"tools/check_{name}_apps.py", "--preset", preset))
                             for name in ("cli", "tls"))
    if profile == "full":
        steps.extend((Step("clangd", (python, "tools/check_clangd.py")),
                      Step("documentation", (python, "tools/docs.py", "build"))))
    return steps


def stop_owned(process: subprocess.Popen) -> None:
    """Terminate only the process group/tree started for this stage, then reap it."""
    if process.poll() is None:
        if WINDOWS:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=10, creationflags=FLAGS, check=False)
        else:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        if process.poll() is None:
            process.kill()
    process.wait(timeout=10)


def run_step(step: Step, directory: Path, timeout: float, root: Path = ROOT) -> dict:
    """Capture output to disk; missing tools, timeouts, skips, and failures stay failures."""
    log_path = directory / f"{step.name}.log"
    result = {"name": step.name, "command": list(step.command), "status": "failed",
              "returncode": None, "log": str(log_path)}
    environment = None
    if step.tests:
        # CTest invokes whole GoogleTest binaries. Inherited selection/sharding
        # must not silently reduce their coverage while the runner claims full suites.
        environment = os.environ.copy()
        selection = ("GTEST_FILTER", "TESTBRIDGE_TEST_ONLY", "GTEST_TOTAL_SHARDS", "GTEST_SHARD_INDEX")
        result["test_selection_removed"] = [key for key in selection if key in environment]
        for key in selection:
            environment.pop(key, None)
    start = time.monotonic()
    with log_path.open("w", encoding="utf-8") as output:
        try:
            process = subprocess.Popen(step.command, cwd=root, stdout=output,
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                       creationflags=FLAGS, start_new_session=not WINDOWS,
                                       env=environment)
        except OSError as error:
            output.write(f"Could not start stage: {error}\n")
            result["reason"] = "command could not start"
        else:
            result["pid"] = process.pid
            try:
                result["returncode"] = process.wait(timeout=timeout)
                result["status"] = "passed" if result["returncode"] == 0 else "failed"
            except subprocess.TimeoutExpired:
                stop_owned(process)
                output.write(f"\nStage timed out after {timeout:g} seconds\n")
                result["status"] = "timed_out"
            except KeyboardInterrupt:
                stop_owned(process)
                output.write("\nStage interrupted\n")
                result["status"] = "interrupted"
    if step.tests and result["status"] == "passed":
        # Verbose CTest includes actual GoogleTest output, so zero/filtered suites
        # cannot produce a green gate merely because their binary exited zero.
        text = log_path.read_text(encoding="utf-8", errors="replace")
        if (not re.search(r"(?:Running|Ran) [1-9]\d* tests?\b", text)
                or re.search(r"No tests were found|(?:Running|Ran) 0 tests|\[\s*SKIPPED\s*\]|"
                             r"YOU HAVE [1-9]\d* DISABLED TESTS?\b|\bskipped=[1-9]\d*", text)):
            result.update(status="failed", reason="empty, skipped, or disabled test coverage")
    result["seconds"] = round(time.monotonic() - start, 3)
    return result


def run_plan(steps: list[Step], directory: Path, timeout: float, root: Path = ROOT) -> list[dict]:
    """Fail fast, retaining explicit not-run records for the remaining stages."""
    results = [{"name": step.name, "command": list(step.command), "status": "not_run"} for step in steps]
    for index, step in enumerate(steps):
        print(f"RUN {step.name}", flush=True)
        results[index] = run_step(step, directory, timeout, root)
        if results[index].get("test_selection_removed"):
            print("RESET inherited test selection: " + ", ".join(results[index]["test_selection_removed"]), flush=True)
        print(f"{results[index]['status'].upper()} {step.name} ({results[index]['seconds']}s)", flush=True)
        if results[index]["status"] != "passed":
            break
    return results


def git_identity(root: Path = ROOT) -> dict:
    """Record revision/status and a content fingerprint, without storing file contents."""
    def git(*args: str) -> bytes:
        return subprocess.check_output(["git", *args], cwd=root, stderr=subprocess.DEVNULL, timeout=30,
                                       creationflags=FLAGS)
    try:
        revision = git("rev-parse", "HEAD").decode().strip()
        status = git("status", "--short").decode("utf-8", errors="replace")
        paths = git("ls-files", "--cached", "--others", "--exclude-standard", "-z").decode("utf-8").split("\0")
        digest = hashlib.sha256()
        for name in sorted(set(paths) - {""}):
            path = root / name
            digest.update(name.encode("utf-8") + b"\0")
            digest.update(hashlib.sha256(path.read_bytes()).digest() if path.is_file() else b"missing")
        return {"revision": revision, "status": status, "fingerprint": digest.hexdigest()}
    except (OSError, subprocess.SubprocessError, UnicodeError) as error:
        return {"error": type(error).__name__}


def main(argv: list[str] | None = None) -> int:
    """Exit 0 for a passing selected scope, 1 for failure, 2 for partial, 130 for interrupt."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=("workflow", "quick", "full"))
    parser.add_argument("--dry-run", action="store_true", help="show commands without executing or creating evidence")
    parser.add_argument("--skip-asan", metavar="REASON", help="record an unavailable sanitizer; full stays partial (exit 2)")
    parser.add_argument("--timeout", type=float, default=1200, help="per-stage deadline in seconds (default 1200)")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        parser.error("timeout must be finite and positive")
    try:
        steps = plan(args.profile, args.skip_asan)
    except ValueError as error:
        parser.error(str(error))
    if args.dry_run:
        print(json.dumps({"status": "dry_run", "profile": args.profile,
                          "skip_asan": args.skip_asan,
                          "steps": [{"name": s.name, "command": list(s.command)} for s in steps]}, indent=2))
        return 0
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    directory = ROOT / ".cache/verification" / f"{stamp}-{args.profile}-{uuid.uuid4().hex[:8]}"
    directory.mkdir(parents=True)
    summary = {"profile": args.profile, "started_utc": stamp, "timeout_seconds": args.timeout,
               "python": sys.version.split()[0], "platform": sys.platform,
               "before": git_identity(), "skip_asan": args.skip_asan, "status": "running"}
    summary_path = directory / "summary.json"
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    summary["steps"] = run_plan(steps, directory, args.timeout)
    summary["after"] = git_identity()
    statuses = {step["status"] for step in summary["steps"]}
    if "interrupted" in statuses:
        summary["status"], code = "interrupted", 130
    elif statuses != {"passed"}:
        summary["status"], code = "failed", 1
    elif "fingerprint" not in summary["before"] or summary["before"] != summary["after"]:
        summary["status"], code = "incomplete", 2
        summary["reason"] = "working tree identity unavailable or changed during verification"
    elif args.skip_asan:
        summary["status"], code = "partial", 2
    else:
        summary["status"], code = "passed", 0
    summary["exit_code"] = code
    summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(f"{summary['status'].upper()} {args.profile}; evidence: {summary_path}", flush=True)
    return code


if __name__ == "__main__":
    sys.exit(main())
