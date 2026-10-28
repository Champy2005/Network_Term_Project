"""Provide two-computer LAN TLS setup, live MCP agent demonstration, stdio MCP bridge, and R1-R11 rehearsal tools."""

import argparse
import json
from pathlib import Path
import socket
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]

MCP_TOOLS_FALLBACK = [
    {"name": "get_status", "description": "Inspect client connection state, active room, and AI model citation.", "inputSchema": {"type": "object", "properties": {}}},
    {"name": "list_users", "description": "List all currently connected usernames on the server.", "inputSchema": {"type": "object", "properties": {}}},
    {"name": "list_groups", "description": "List all discoverable chat groups and their current members.", "inputSchema": {"type": "object", "properties": {}}},
    {"name": "list_rooms", "description": "List all local room views in the client model.", "inputSchema": {"type": "object", "properties": {}}},
    {"name": "open_room", "description": "Select and display a direct user room or group room in the client UI.", "inputSchema": {"type": "object", "properties": {"target": {"type": "string"}}, "required": ["target"]}},
    {"name": "get_room_messages", "description": "Read the transcript lines in a room view.", "inputSchema": {"type": "object", "properties": {"room": {"type": "string"}}}},
    {"name": "load_room_history", "description": "Request stored SQLite history from the server for a room.", "inputSchema": {"type": "object", "properties": {"room_id": {"type": "string"}}}},
    {"name": "create_group", "description": "Create a new group with only this client as the initial member.", "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}}, "required": ["name"]}},
    {"name": "join_group", "description": "Explicitly self-join an existing group by group_id or group name.", "inputSchema": {"type": "object", "properties": {"group_id": {"type": "string"}}, "required": ["group_id"]}},
    {"name": "send_direct_message", "description": "Send a private direct message to a connected user.", "inputSchema": {"type": "object", "properties": {"recipient": {"type": "string"}, "text": {"type": "string"}}, "required": ["recipient", "text"]}},
    {"name": "send_group_message", "description": "Send a message to a joined group by group_id or group name.", "inputSchema": {"type": "object", "properties": {"group_id": {"type": "string"}, "text": {"type": "string"}}, "required": ["group_id", "text"]}},
    {"name": "summarize_room", "description": "Summarize a room transcript using TextRank centrality and VADER sentiment analysis.", "inputSchema": {"type": "object", "properties": {"room": {"type": "string"}, "max_sentences": {"type": "integer"}}}},
    {"name": "extract_action_items", "description": "Extract tasks, decisions, and open questions from a room transcript.", "inputSchema": {"type": "object", "properties": {"room": {"type": "string"}}}},
    {"name": "suggest_smart_reply", "description": "Generate a context-aware smart reply and stage or send it.", "inputSchema": {"type": "object", "properties": {"auto_send": {"type": "boolean"}}}},
    {"name": "polish_draft", "description": "Rewrite and polish a message draft for clarity and tone.", "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}}},
    {"name": "analyze_sentiment", "description": "Analyze sentiment polarity of text or the active room transcript.", "inputSchema": {"type": "object", "properties": {"text": {"type": "string"}}}},
    {"name": "run_agent_prompt", "description": "Execute a natural-language command or multi-step AI query against the chat client.", "inputSchema": {"type": "object", "properties": {"prompt": {"type": "string"}}, "required": ["prompt"]}},
]


def send_jsonrpc(sock: socket.socket, payload: dict) -> dict:
    """Send one newline-delimited JSON-RPC 2.0 message and read one JSON-RPC response line."""
    raw = (json.dumps(payload) + "\n").encode("utf-8")
    sock.sendall(raw)
    buffer = b""
    while b"\n" not in buffer:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("Agent control connection closed before response")
        buffer += chunk
    line = buffer.split(b"\n", 1)[0].decode("utf-8")
    return json.loads(line)


def run_mcp_bridge(port: int) -> int:
    """Bridge stdio MCP JSON-RPC 2.0 requests (from Antigravity/Claude/Cursor) to 127.0.0.1:<port>."""
    for raw_line in sys.stdin:
        line = raw_line.lstrip("\ufeff").strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except json.JSONDecodeError:
            sys.stdout.write('{"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error"}}\n')
            sys.stdout.flush()
            continue

        method = req.get("method", "")
        req_id = req.get("id")
        if req_id is None and method.startswith("notifications/"):
            continue

        try:
            with socket.create_connection(("127.0.0.1", port), timeout=2.5) as sock:
                resp = send_jsonrpc(sock, req)
                sys.stdout.write(json.dumps(resp) + "\n")
                sys.stdout.flush()
                continue
        except OSError as exc:
            if method == "initialize":
                fallback = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "protocolVersion": "2024-11-05",
                        "capabilities": {"tools": {}},
                        "serverInfo": {"name": "socket-chat-mcp", "version": "0.1.0"},
                    },
                }
                sys.stdout.write(json.dumps(fallback) + "\n")
                sys.stdout.flush()
            elif method == "ping":
                sys.stdout.write(json.dumps({"jsonrpc": "2.0", "id": req_id, "result": {}}) + "\n")
                sys.stdout.flush()
            elif method == "tools/list":
                sys.stdout.write(
                    json.dumps({"jsonrpc": "2.0", "id": req_id, "result": {"tools": MCP_TOOLS_FALLBACK}}) + "\n"
                )
                sys.stdout.flush()
            else:
                err = {
                    "jsonrpc": "2.0",
                    "id": req_id,
                    "result": {
                        "content": [{
                            "type": "text",
                            "text": f"Cannot reach chat_gui/chat_cli on 127.0.0.1:{port} ({exc}). Start chat_gui.exe with '--agent-port {port}'.",
                        }],
                        "isError": True,
                    },
                }
                sys.stdout.write(json.dumps(err) + "\n")
                sys.stdout.flush()
    return 0


def run_agent_demo(port: int, peer: str, group_name: str) -> int:
    """Connect to a live GUI/CLI --agent-port on 127.0.0.1 and demonstrate MCP tool control."""
    print(f"Connecting external MCP AI Agent to 127.0.0.1:{port}...")
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        init = send_jsonrpc(sock, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}})
        print(f"[MCP initialize] serverInfo = {json.dumps(init.get('result', {}).get('serverInfo'))}")

        tools = send_jsonrpc(sock, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
        tool_names = [t["name"] for t in tools.get("result", {}).get("tools", [])]
        print(f"[MCP tools/list] {len(tool_names)} tools available: {', '.join(tool_names)}")

        status = send_jsonrpc(sock, {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "tools/call",
            "params": {"name": "get_status", "arguments": {}},
        })
        print(f"[MCP get_status] {status['result']['content'][0]['text']}")

        create = send_jsonrpc(sock, {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "tools/call",
            "params": {"name": "create_group", "arguments": {"name": group_name}},
        })
        print(f"[MCP create_group] {create['result']['content'][0]['text']}")
        time.sleep(0.25)

        if peer:
            dm = send_jsonrpc(sock, {
                "jsonrpc": "2.0",
                "id": 5,
                "method": "tools/call",
                "params": {
                    "name": "send_direct_message",
                    "arguments": {
                        "recipient": peer,
                        "text": "Hello from the external MCP AI Agent! TLS 1.3 socket framing works great!",
                    },
                },
            })
            print(f"[MCP send_direct_message] {dm['result']['content'][0]['text']}")
            time.sleep(0.25)
            send_jsonrpc(sock, {
                "jsonrpc": "2.0",
                "id": 6,
                "method": "tools/call",
                "params": {"name": "open_room", "arguments": {"target": peer}},
            })

        summary = send_jsonrpc(sock, {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "tools/call",
            "params": {"name": "summarize_room", "arguments": {"max_sentences": 3}},
        })
        print(f"[MCP summarize_room] {summary['result']['content'][0]['text']}")

        sentiment = send_jsonrpc(sock, {
            "jsonrpc": "2.0",
            "id": 8,
            "method": "tools/call",
            "params": {
                "name": "analyze_sentiment",
                "arguments": {"text": "The C++20 TLS 1.3 socket chat and AI agent integration are awesome!"},
            },
        })
        print(f"[MCP analyze_sentiment] {sentiment['result']['content'][0]['text']}")
    return 0


def lan_setup(ip: str, port: int, preset: str) -> int:
    """Prepare SAN certificates for a two-computer LAN demo and print machine commands."""
    cert_dir = ROOT / "certs" / f"lan-{ip.replace('.', '-').replace(':', '-')}"
    if not (cert_dir / "ca.pem").is_file():
        subprocess.run(
            [sys.executable, str(ROOT / "tools/dev_tls.py"), "--output", str(cert_dir), "--ip", ip],
            cwd=ROOT,
            check=True,
        )
    rel_cert = cert_dir.relative_to(ROOT).as_posix()
    print("\n=== Two-Physical-Computer LAN Demo Commands ===")
    print(f"1. Copy ONLY '{rel_cert}/ca.pem' to Computer B.")
    print(f"\n2. On Computer A (Server + Alice GUI, LAN IP {ip}):")
    print(f"   build/{preset}/chat_server --tls {rel_cert}/server.pem {rel_cert}/server.key 0.0.0.0 {port} data/chat.sqlite3")
    print(f"   build/{preset}/chat_gui --tls {rel_cert}/ca.pem --agent-port 7777")
    print(f"\n3. On Computer B (Bob GUI + Charlie GUI/CLI connecting to {ip}:{port}):")
    print(f"   build/{preset}/chat_gui --tls {rel_cert}/ca.pem")
    print(f"   build/{preset}/chat_cli --tls {rel_cert}/ca.pem chat {ip} {port} charlie")
    print("\n4. Live External AI Agent Control (on Computer A while chat_gui is open):")
    print("   python tools/demo.py agent-demo --port 7777 --peer bob\n")
    return 0


def print_checklist() -> int:
    """Print the graded R1-R11 + Part 3 Special Points live demonstration script."""
    steps = [
        ("R1 (1.0)", "Show build/slides/index.html (Slides 3-4): 2 physical computers over LAN, explain Protobuf, 4-byte framing, TLS 1.3, TCP, IP."),
        ("R2 (0.5)", "Show Slide 5 snapshot (ClientSession async_read_some / async_write) and live socket traffic."),
        ("R3 (0.5)", "Connect Alice (Computer A) and Bob (Computer B). Try connecting a second 'alice' -> rejected by server."),
        ("R4 (0.5)", "Connect Charlie. Show all 3 clients list ['alice', 'bob', 'charlie'] including their own username."),
        ("R5 (0.5)", "Open Direct: bob and Group room views in Alice's GUI; show separate transcripts and drafts."),
        ("R6 (0.5)", "Point out the per-room transcript window, message input box, and Send button in both Direct and Group rooms."),
        ("R7 (1.0)", "Alice sends a Direct Message to Bob. Verify Alice and Bob display it, while Charlie's window/CLI receives nothing."),
        ("R8 (1.0)", "Alice creates group 'Study'. Verify initial member list shows only 'alice'."),
        ("R9 (1.0)", "Verify Alice, Bob, and non-member Charlie all see group 'Study' and its member list."),
        ("R10 (1.0)", "Bob clicks 'Join selected group' (or /join). Charlie does not join. Show updated member list ['alice', 'bob']."),
        ("R11 (1.0)", "Bob sends a message to 'Study'. Alice and Bob see it; Charlie receives nothing and non-member send is rejected."),
        ("Special 1 (AI, 1.0)", "Click 'Summarize room' and 'Sentiment' in GUI (or /summarize, /sentiment in CLI); show TextRank + VADER output and Slide 17 reference."),
        ("Special 2 (MCP, 1.0)", "Run 'python tools/demo.py agent-demo --port 7777 --peer bob' to watch an external AI agent control the live GUI via JSON-RPC 2.0."),
        ("Special 3 (TLS 1.3, 1.0)", "Show TLS 1.3 SAN verification in server/CLI logs and Slide 19 code snapshot."),
        ("Special 4 (SQLite/Sync, 1.0)", "Click 'Load history' in GUI (or reconnect after drop) to show SQLite/WAL persistence and gapless sequence recovery (Slide 20)."),
    ]
    print("=== Graded Live Demonstration Checklist (12.5 Points Total) ===")
    for code, desc in steps:
        print(f"- [{code}] {desc}")
    return 0


def main() -> int:
    """Dispatch LAN setup, external MCP agent demo, stdio MCP bridge, or grading checklist."""
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    lan = sub.add_parser("lan-setup", help="Generate LAN IP SAN certificates and print 2-PC commands")
    lan.add_argument("--ip", required=True, help="LAN IPv4/IPv6 address of the server computer")
    lan.add_argument("--port", type=int, default=5555)
    lan.add_argument("--preset", default="debug", choices=("debug", "release", "asan"))

    agent = sub.add_parser("agent-demo", help="Drive a running GUI/CLI over its loopback --agent-port")
    agent.add_argument("--port", type=int, default=7777)
    agent.add_argument("--peer", default="bob")
    agent.add_argument("--group", default="AI Agents")

    bridge = sub.add_parser("mcp-bridge", help="Stdio-to-TCP MCP JSON-RPC 2.0 bridge for Antigravity/Claude/Cursor")
    bridge.add_argument("--port", type=int, default=7777)

    sub.add_parser("checklist", help="Print the step-by-step R1-R11 + Part 3 live demo checklist")

    args = parser.parse_args()
    if args.command == "lan-setup":
        return lan_setup(args.ip, args.port, args.preset)
    if args.command == "agent-demo":
        return run_agent_demo(args.port, args.peer, args.group)
    if args.command == "mcp-bridge":
        return run_mcp_bridge(args.port)
    return print_checklist()


if __name__ == "__main__":
    raise SystemExit(main())
