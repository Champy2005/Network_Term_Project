"""Verify real persistent CLI core flows, idle input, privacy, EOF, and failure with hidden processes."""
import argparse
from pathlib import Path
import queue
import re
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
FLAGS = subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0


class Process:
    """Own one subprocess and bounded output collection; terminate only this process on cleanup."""
    def __init__(self, arguments: list[str]):
        self.process = subprocess.Popen(arguments, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, text=True, encoding="utf-8", creationflags=FLAGS)
        self.lines = queue.Queue(maxsize=4096)
        self.seen = []
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self) -> None:
        """Collect bounded synthetic test output without displaying chat data."""
        for line in self.process.stdout:
            try:
                self.lines.put(line, timeout=1)
            except queue.Full:
                return

    def drain(self) -> None:
        """Move only bounded available output into the test observer."""
        while len(self.seen) < 4096:
            try:
                self.seen.append(self.lines.get_nowait())
            except queue.Empty:
                return
        raise AssertionError("CLI output exceeded smoke-test budget")

    def wait_line(self, pattern: str) -> str:
        """Wait for a status/event with a deadline independent of blocked stdin."""
        end = time.monotonic() + 5
        while time.monotonic() < end:
            self.drain()
            for line in self.seen:
                if re.search(pattern, line):
                    return line
            time.sleep(0.005)
        raise AssertionError(f"Missing output pattern: {pattern}")

    def send(self, text: str) -> None:
        """Write synthetic commands, preserving actual newline and Unicode bytes."""
        self.process.stdin.write(text)
        self.process.stdin.flush()

    def close(self) -> None:
        """Bound cleanup and join the reader after its owned process closes output."""
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(timeout=5)
        self.reader.join(timeout=1)
        for stream in (self.process.stdin, self.process.stdout):
            if stream and not stream.closed:
                stream.close()


def exercise(preset: str, encrypted: bool) -> None:
    """Run three CLI clients through private DM, creator-only groups, join, and member delivery."""
    suffix = ".exe" if FLAGS else ""
    executable = str(ROOT / "build" / preset / f"chat_cli{suffix}")
    server_executable = str(ROOT / "build" / preset / f"chat_server{suffix}")
    certificates = ROOT / "certs/development"
    server_mode = (["--tls", str(certificates / "server.pem"), str(certificates / "server.key")]
                   if encrypted else ["--plaintext-dev"])
    client_mode = ["--tls", str(certificates / "ca.pem")] if encrypted else ["--plaintext-dev"]
    owned = []
    try:
        server = Process([server_executable, *server_mode, "127.0.0.1", "0", ":memory:"])
        owned.append(server)
        line = server.wait_line(r"listening on 127\.0\.0\.1:(\d+)")
        port = re.search(r"127\.0\.0\.1:(\d+)", line).group(1)
        clients = []
        for name in ("alice", "bob", "charlie"):
            client = Process([executable, *client_mode, "chat", "127.0.0.1", port, name])
            clients.append(client)
            owned.append(client)
            client.wait_line(f'^CONNECTED "{name}"')
        alice, bob, charlie = clients
        alice.wait_line(r'^USERS .*"charlie"')
        # Stdin remains open and idle while incoming updates must still appear.
        alice.send("/dm bob smoke_private_ไทย\n")
        alice.wait_line(r"^OK 1 dm")
        bob.wait_line(r"^DM .*smoke_private_ไทย")
        alice.send("/create Study group\n")
        line = alice.wait_line(r'^OK 2 create room="([^"]+)"')
        group = re.search(r'room="([^"]+)"', line).group(1)
        charlie.wait_line(rf'^GROUP "{group}" "Study group" MEMBERS "alice"$')
        bob.send(f"/group {group} forbidden_smoke\n")
        bob.wait_line(r"^REJECTED 1 group code=7")
        bob.send(f"/join {group}\n")
        bob.wait_line(r"^OK 2 join")
        bob.send(f"/group {group} smoke_joined\n")
        bob.wait_line(r"^OK 3 group")
        alice.wait_line(r"^GROUP_MESSAGE .*smoke_joined")
        time.sleep(0.1)
        charlie.drain()
        assert all("smoke_private" not in line and "smoke_joined" not in line and
                   "forbidden_smoke" not in line for line in charlie.seen)
        alice.send("/dm alice eof_receipt")  # Unterminated final command must execute at EOF.
        alice.process.stdin.close()
        assert alice.process.wait(timeout=5) == 0
        alice.wait_line(r"^OK 3 dm")
        mcp = Process([executable, *client_mode, "mcp", "127.0.0.1", port, "mcp_agent"])
        owned.append(mcp)
        mcp.send('{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n')
        mcp.wait_line(r'"protocolVersion":"2024-11-05"')
        mcp.send('{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"send_direct_message","arguments":{"recipient":"bob","text":"mcp_smoke_ok"}}}\n')
        mcp.wait_line(r'"isError":false')
        bob.wait_line(r"^DM .*mcp_smoke_ok")
        mcp.process.stdin.close()
        assert mcp.process.wait(timeout=5) == 0
        for client, code in ((bob, 1), (charlie, 0)):
            client.send("/quit\n/create must_not_run\n")
            assert client.process.wait(timeout=5) == code
        # Initial rejection must exit while stdin stays open, with no input-reader join hang.
        bad_mode = (["--tls", str(certificates / "ca.pem"), "--server-name", "wrong.example"]
                    if encrypted else client_mode)
        bad_name = "failure" if encrypted else "bad-name"
        bad = Process([executable, *bad_mode, "chat", "127.0.0.1", port, bad_name])
        owned.append(bad)
        assert bad.process.wait(timeout=5) == 1
        # A refused connection must also end without consuming/closing stdin first.
        server.close()
        refused = Process([executable, *client_mode, "chat", "127.0.0.1", port, "refused"])
        owned.append(refused)
        assert refused.process.wait(timeout=5) == 1
        print(f"CLI core flows/privacy, idle stdin, EOF drain, quit, and failure pass: {server_mode[0]}")
    finally:
        for process in reversed(owned):
            process.close()


def main() -> int:
    """Verify both explicit transport modes against the selected built preset."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", choices=("debug", "release", "asan"), default="debug")
    args = parser.parse_args()
    exercise(args.preset, False)
    exercise(args.preset, True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
