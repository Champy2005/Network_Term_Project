"""Exercise TLS server/CLI processes and explicit development-mode rejection without visible windows."""
import argparse
from pathlib import Path
import queue
import re
import socket
import ssl
import subprocess
import threading

ROOT = Path(__file__).resolve().parents[1]
FLAGS = subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0


def run(command: list[str]) -> subprocess.CompletedProcess:
    """Run one bounded diagnostic subprocess and capture only ordinary status output."""
    return subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=5, creationflags=FLAGS)


def main() -> int:
    """Verify negotiation, login/Ping, rejected trust/name/modes, and loopback development."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("debug", "release", "asan"), default="debug")
    parser.add_argument("--cert-dir", type=Path, default=ROOT / "certs/development")
    args = parser.parse_args()
    suffix = ".exe" if FLAGS else ""
    server = str(ROOT / "build" / args.preset / f"chat_server{suffix}")
    client = str(ROOT / "build" / args.preset / f"chat_cli{suffix}")
    certificates = args.cert_dir.resolve()
    for name in ("ca.pem", "server.pem", "server.key"):
        if not (certificates / name).is_file():
            parser.error("generate certificates first: python tools/dev_tls.py")
    for command in ([server], [client, "127.0.0.1", "5555"],
                    [server, "--plaintext-dev", "0.0.0.0", "0", ":memory:"],
                    [client, "--tls", str(certificates / "ca.pem"), "--plaintext-dev"],
                    [server, "--tls", str(certificates / "server.pem"), str(certificates / "missing.key"),
                     "127.0.0.1", "0", ":memory:"]):
        result = run(command)
        assert result.returncode == 1, result
    print("Missing/mixed modes, nonloopback plaintext, and missing credentials rejected")
    for mode in (["--tls", str(certificates / "server.pem"), str(certificates / "server.key")],
                 ["--plaintext-dev"]):
        process = subprocess.Popen([server, *mode, "127.0.0.1", "0", ":memory:"], cwd=ROOT,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, creationflags=FLAGS)
        try:
            lines = queue.Queue()
            threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
            line = lines.get(timeout=5)
            match = re.search(r"listening on 127\.0\.0\.1:(\d+)", line)
            assert match, line
            port = match.group(1)
            if mode[0] == "--tls":
                assert "TLS1.3" in line, line
                trust = ssl.create_default_context(cafile=str(certificates / "ca.pem"))
                trust.minimum_version = trust.maximum_version = ssl.TLSVersion.TLSv1_3
                with socket.create_connection(("127.0.0.1", int(port)), timeout=2) as raw:
                    with trust.wrap_socket(raw, server_hostname="localhost") as encrypted:
                        assert encrypted.version() == "TLSv1.3"
                bad = run([client, "--tls", str(certificates / "ca.pem"), "--server-name", "wrong.example",
                           "127.0.0.1", port, "42", "smoke"])
                assert bad.returncode == 1 and "TLS" in bad.stderr, bad
                client_mode = ["--tls", str(certificates / "ca.pem"), "--server-name", "localhost"]
            else:
                assert "plaintext-development-loopback" in line, line
                client_mode = ["--plaintext-dev"]
            result = run([client, *client_mode, "127.0.0.1", port, "42", "smoke"])
            assert result.returncode == 0 and "Logged in as smoke" in result.stdout and "Pong nonce=42" in result.stdout, result
            print(f"Server/CLI startup, login and Ping pass: {mode[0]}")
        finally:
            process.terminate()
            process.wait(timeout=3)
    print("TLS application smoke checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
