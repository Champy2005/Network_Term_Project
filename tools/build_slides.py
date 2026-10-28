"""Generate and verify the 20-slide Implementation Explanation deck and code snapshots."""

import argparse
import html
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "build/slides"

SLIDE_SNAPSHOTS = [
    {
        "slide": 5,
        "req": "R2",
        "title": "R2 – Socket Programming Only (Framed Async Read/Write Path)",
        "primary": ("src/server/client_session.cpp", 161, 215, "Async stream read + 4-byte length-frame decoding"),
        "secondary": ("src/server/client_session.cpp", 217, 258, "Single-writer ordered async_write queue"),
    },
    {
        "slide": 7,
        "req": "R3",
        "title": "R3 – Each client must have a unique name",
        "primary": ("src/server/chat_hub.cpp", 187, 227, "Server-authoritative unique username reservation"),
        "secondary": ("src/server/chat_hub.cpp", 67, 86, "Disconnect cleanup releasing active username"),
    },
    {
        "slide": 8,
        "req": "R4",
        "title": "R4 – Connected-user list including the client's own name",
        "primary": ("src/server/chat_hub.cpp", 457, 491, "Server UserListUpdate snapshot broadcast to all registered clients"),
        "secondary": ("apps/gui/window.cpp", 324, 340, "GUI connected-user list box update"),
    },
    {
        "slide": 9,
        "req": "R5",
        "title": "R5 – Separate chat room per Private and Group conversation",
        "primary": ("src/client/room_model.cpp", 50, 89, "Independent Direct and Group RoomView management"),
        "secondary": ("src/client/room_model.cpp", 184, 218, "Per-room message routing by server room_id / group_id"),
    },
    {
        "slide": 10,
        "req": "R6",
        "title": "R6 – Per-room chat box, send button, and transcript window",
        "primary": ("apps/gui/window.cpp", 124, 154, "Native Win32 transcript, draft box, and Send controls"),
        "secondary": ("apps/gui/window.cpp", 365, 386, "Transcript rendering and draft preservation per room"),
    },
    {
        "slide": 11,
        "req": "R7",
        "title": "R7 – Private Direct Message (Only sender and receiver can see)",
        "primary": ("src/server/chat_hub.cpp", 229, 288, "Server pair-room routing to sender and recipient only"),
        "secondary": ("apps/gui/window.cpp", 242, 255, "GUI direct-room selection and message submission"),
    },
    {
        "slide": 12,
        "req": "R8",
        "title": "R8 – Create chat group (Creator is sole initial member)",
        "primary": ("src/server/chat_hub.cpp", 290, 302, "Server group creation initializing members to {&session}"),
        "secondary": ("apps/gui/window.cpp", 232, 241, "GUI group creation request submission"),
    },
    {
        "slide": 13,
        "req": "R9",
        "title": "R9 – Every client sees all existing groups and their member lists",
        "primary": ("src/server/chat_hub.cpp", 466, 482, "Complete GroupListUpdate snapshot with sorted members"),
        "secondary": ("apps/gui/window.cpp", 346, 360, "GUI group list and selected-group member display"),
    },
    {
        "slide": 14,
        "req": "R10",
        "title": "R10 – Voluntary self-join only (Never auto-added by creator)",
        "primary": ("src/server/chat_hub.cpp", 304, 319, "Explicit JoinGroupRequest admitting only the requesting session"),
        "secondary": ("apps/gui/window.cpp", 234, 237, "GUI explicit self-join button action"),
    },
    {
        "slide": 15,
        "req": "R11",
        "title": "R11 – Group messaging restricted to current group members only",
        "primary": ("src/server/chat_hub.cpp", 321, 375, "Server membership check and member-only fanout"),
        "secondary": ("src/client/chat_worker.cpp", 370, 381, "Worker keyed group send over the single client session"),
    },
    {
        "slide": 17,
        "req": "Special Point 1 (AI)",
        "title": "Special Point 1 (AI) – TextRank Summarization, VADER Sentiment & Agent Router",
        "model": "TextRank (Mihalcea & Tarau, EMNLP 2004) + VADER Lexicon (Hutto & Gilbert, ICWSM 2014)",
        "primary": ("src/ai/ai_engine.cpp", 139, 209, "VADER-inspired valence sentiment with negation & booster rules"),
        "secondary": ("src/ai/ai_engine.cpp", 211, 312, "TF-IDF cosine similarity & power-iteration TextRank centrality"),
    },
    {
        "slide": 18,
        "req": "Special Point 2",
        "title": "Special Point 2 – External AI Agent Control Protocol (MCP JSON-RPC 2.0 & GUI Port)",
        "primary": ("src/ai/agent_tools.cpp", 536, 586, "Model Context Protocol (MCP) JSON-RPC 2.0 tool dispatcher"),
        "secondary": ("src/ai/agent_tools.cpp", 588, 675, "Non-blocking loopback (127.0.0.1) live GUI/CLI agent server"),
    },
    {
        "slide": 19,
        "req": "Special Point 3",
        "title": "Special Point 3 – Verified TLS 1.3 Transport with X.509 CA & DNS/IP SAN Checks",
        "primary": ("src/transport/tls.cpp", 53, 91, "TLS 1.3 context setup and strict X.509 DNS/IP SAN verification"),
        "secondary": ("src/server/client_session.cpp", 35, 62, "Bounded server TLS 1.3 handshake deadline and session activation"),
    },
    {
        "slide": 20,
        "req": "Special Point 4",
        "title": "Special Point 4 – SQLite/WAL Transactional History, Resume & Sequence Sync",
        "primary": ("src/server/history_store.cpp", 128, 180, "SQLite WAL schema v2, hashed bearer tokens & atomic transactions"),
        "secondary": ("src/client/chat_worker.cpp", 187, 240, "Automatic contiguous sequence catch-up and on-demand room history"),
    },
]


def extract_snippet(rel_path: str, start_line: int, end_line: int) -> str:
    """Read a 1-based inclusive line range and prefix each line with its exact line number."""
    path = ROOT / rel_path
    if not path.is_file():
        raise FileNotFoundError(f"Missing source file for snapshot: {rel_path}")
    lines = path.read_text(encoding="utf-8").splitlines()
    if start_line < 1 or end_line > len(lines) or start_line > end_line:
        raise ValueError(
            f"Invalid line range {start_line}..{end_line} in {rel_path} (total {len(lines)} lines)"
        )
    formatted = []
    for number in range(start_line, end_line + 1):
        formatted.append(f"{number:4d} | {lines[number - 1]}")
    return "\n".join(formatted)


def verify_snapshots() -> list[dict]:
    """Validate all slide source files and line ranges against the working tree."""
    verified = []
    for item in SLIDE_SNAPSHOTS:
        p_file, p_start, p_end, p_caption = item["primary"]
        s_file, s_start, s_end, s_caption = item["secondary"]
        p_code = extract_snippet(p_file, p_start, p_end)
        s_code = extract_snippet(s_file, s_start, s_end)
        verified.append({
            **item,
            "primary_code": p_code,
            "secondary_code": s_code,
            "primary_header": f"{p_file}:{p_start}-{p_end} — {p_caption}",
            "secondary_header": f"{s_file}:{s_start}-{s_end} — {s_caption}",
        })
    return verified


def render_html(verified: list[dict]) -> str:
    """Render a self-contained 20-slide HTML presentation matching the assignment template."""
    by_slide = {entry["slide"]: entry for entry in verified}
    slides_html = []

    # Slide 1: Cover
    slides_html.append("""
    <section class="slide cover">
      <h1>Term Project: Socket Programming</h1>
      <h2>Authoritative C++20 TLS 1.3 Chat System with AI Agent &amp; MCP Control</h2>
      <p class="meta">Separate Server (<code>chat_server</code>), Native Windows Room GUI (<code>chat_gui</code>), and Persistent/MCP CLI (<code>chat_cli</code>)</p>
    </section>""")

    # Slide 2: Part 1 Divider
    slides_html.append("""
    <section class="slide divider">
      <h1>Part 1 System Architecture Design</h1>
      <p class="warn">This part must be fully completed; otherwise, Part 2 will not be graded.</p>
    </section>""")

    # Slide 3: R1 Architecture Diagram
    slides_html.append("""
    <section class="slide">
      <h2>R1 – System Architecture Design</h2>
      <div class="arch-box">
        <pre class="diagram">
[Computer A: Windows GUI / CLI (Alice)] ──┐
[External MCP AI Agent (127.0.0.1)]       ├── TCP + TLS 1.3 + 4-Byte Length Frame + Protobuf ──► [Server: ClientSession &amp; Authoritative ChatHub] ──► [SQLite/WAL Store v2]
[Computer B: Windows GUI / CLI (Bob)]   ──┘                                                       ├─ Unique Active Usernames &amp; Presence Snapshots
[Client 3: GUI / CLI (Charlie)]         ──┘                                                       ├─ Pair-Scoped Direct Rooms (Sender + Recipient Only)
                                                                                                  └─ Voluntary Self-Join Group Rooms (Members Only)
        </pre>
      </div>
      <ul>
        <li><strong>Separate Executables:</strong> <code>apps/server/main.cpp</code> (<code>chat_server</code>) vs. <code>apps/gui/main.cpp</code> (<code>chat_gui</code>) and <code>apps/cli/main.cpp</code> (<code>chat_cli</code>).</li>
        <li><strong>Authoritative Hub Strand:</strong> All username reservations, direct pair routing, group memberships, and SQLite WAL commits serialize on one shared Boost.Asio strand.</li>
        <li><strong>UI Independence:</strong> Both the Win32 GUI and CLI use the portable <code>ChatWorker</code> and <code>RoomModel</code>; privacy is enforced strictly by the server.</li>
      </ul>
    </section>""")

    # Slide 4: R1 Protocol Explanation
    slides_html.append("""
    <section class="slide">
      <h2>R1 – Computer Network Protocols Used</h2>
      <table>
        <thead>
          <tr><th>Protocol / Layer</th><th>What It Is</th><th>Why It Was Selected</th><th>How It Is Used in Socket Chat</th></tr>
        </thead>
        <tbody>
          <tr><td><strong>Protobuf v1</strong></td><td>Typed binary schema (<code>proto/chat.proto</code>)</td><td>Compact, strongly-typed payloads with forward-safe tags</td><td>Serializes <code>Envelope</code> messages (login, DM, group, snapshots, history, sync, ping/pong)</td></tr>
          <tr><td><strong>4-Byte Framing</strong></td><td>Big-endian uint32 length prefix (1..1,048,576 B)</td><td>TCP is a byte stream without message boundaries</td><td><code>FrameDecoder</code> reassembles split/coalesced TCP reads before Protobuf parsing</td></tr>
          <tr><td><strong>TLS 1.3 (OpenSSL)</strong></td><td>Encrypted, authenticated transport layer</td><td>Prevents eavesdropping/tampering and verifies server identity</td><td>Validates X.509 CA chain and DNS/IP SAN on every client-server connection</td></tr>
          <tr><td><strong>TCP (Boost.Asio)</strong></td><td>Reliable, ordered, connection-oriented transport</td><td>Chat requires ordered byte delivery and connection lifecycle detection</td><td>Persistent full-duplex socket per client with one bounded FIFO write queue</td></tr>
          <tr><td><strong>IPv4 / IPv6 &amp; Wi-Fi/LAN</strong></td><td>Network addressing and physical link</td><td>Routes packets across two or more physical computers</td><td>Clients connect to the server's LAN IP and TCP port across computers</td></tr>
        </tbody>
      </table>
    </section>""")

    for slide_num in range(5, 21):
        if slide_num == 6:
            slides_html.append("""
    <section class="slide divider">
      <h1>Part 2 Fundamental Implementation (R3–R11)</h1>
      <p class="warn">This part must be fully completed; otherwise, Part 3 will not be graded.</p>
    </section>""")
            continue
        if slide_num == 16:
            slides_html.append("""
    <section class="slide divider">
      <h1>Part 3 Special Points (4 Features, Including AI)</h1>
      <p class="warn">Demonstrating AI integration, External Agent MCP Control, TLS 1.3, and SQLite/WAL Recovery.</p>
    </section>""")
            continue
        entry = by_slide[slide_num]
        model_html = (
            f'<p class="model-ref"><strong>AI Model &amp; Reference:</strong> {html.escape(entry["model"])}</p>'
            if "model" in entry else ""
        )
        slides_html.append(f"""
    <section class="slide">
      <h2>Slide {slide_num}: {html.escape(entry["title"])}</h2>
      {model_html}
      <div class="grid-two">
        <div class="code-card">
          <div class="card-header">Code Snapshot (Mandatory): {html.escape(entry["primary_header"])}</div>
          <pre><code>{html.escape(entry["primary_code"])}</code></pre>
        </div>
        <div class="code-card">
          <div class="card-header">Supporting Snapshot: {html.escape(entry["secondary_header"])}</div>
          <pre><code>{html.escape(entry["secondary_code"])}</code></pre>
        </div>
      </div>
      <div class="prompt-footer">Generative AI Prompt: Scoped C++20 implementation and verification following repository architecture and R1-R11 acceptance criteria.</div>
    </section>""")

    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>Socket Chat – Implementation Explanation Slides (1–20)</title>
  <style>
    * {{ box-sizing: border-box; }}
    body {{ margin: 0; font-family: 'Segoe UI', system-ui, sans-serif; background: #0f172a; color: #f8fafc; }}
    .slide {{ width: 100%; min-height: 100vh; padding: 32px 44px; border-bottom: 2px solid #334155; display: flex; flex-direction: column; justify-content: space-between; page-break-after: always; }}
    .cover, .divider {{ justify-content: center; align-items: center; text-align: center; }}
    h1 {{ font-size: 2.4rem; margin: 0 0 12px 0; color: #38bdf8; }}
    h2 {{ font-size: 1.5rem; margin: 0 0 10px 0; color: #7dd3fc; }}
    .warn {{ color: #fca5a5; font-size: 1.2rem; font-weight: 600; }}
    .model-ref {{ margin: 0 0 8px 0; font-size: 0.95rem; color: #fde047; }}
    .grid-two {{ display: grid; grid-template-columns: 1fr 1fr; gap: 16px; flex: 1; min-height: 0; }}
    .code-card {{ background: #1e293b; border: 1px solid #475569; border-radius: 8px; display: flex; flex-direction: column; overflow: hidden; }}
    .card-header {{ background: #334155; padding: 6px 12px; font-size: 0.82rem; font-weight: 600; color: #e2e8f0; }}
    pre {{ margin: 0; padding: 10px 12px; font-family: 'Cascadia Code', Consolas, monospace; font-size: 0.72rem; line-height: 1.32; overflow: auto; color: #f1f5f9; }}
    .arch-box {{ background: #1e293b; border: 1px solid #475569; border-radius: 8px; padding: 12px; margin-bottom: 12px; }}
    .diagram {{ font-size: 0.85rem; line-height: 1.45; color: #a7f3d0; }}
    table {{ width: 100%; border-collapse: collapse; background: #1e293b; font-size: 0.9rem; }}
    th, td {{ border: 1px solid #475569; padding: 10px 12px; text-align: left; }}
    th {{ background: #334155; color: #38bdf8; }}
    .prompt-footer {{ margin-top: 8px; padding: 6px 10px; background: #1e293b; border-left: 3px solid #38bdf8; font-size: 0.78rem; color: #94a3b8; }}
    @media print {{
      body {{ background: #ffffff; color: #0f172a; }}
      .slide {{ height: 100vh; border: none; }}
    }}
  </style>
</head>
<body>
{''.join(slides_html)}
</body>
</html>
"""


def main() -> int:
    """Validate all 20 slide snapshots or generate build/slides/index.html."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Verify snapshot files and line numbers only")
    parser.add_argument("--output", type=Path, default=OUTPUT_DIR / "index.html")
    args = parser.parse_args()
    try:
        verified = verify_snapshots()
        if args.check:
            print(f"PASS verified {len(verified)} slide code-snapshot pairs across 20 template slides")
            return 0
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(render_html(verified), encoding="utf-8")
        print(f"Generated 20-slide deck: {args.output}")
        return 0
    except (OSError, ValueError) as error:
        print(f"Slide build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
