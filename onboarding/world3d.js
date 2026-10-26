// ============================================================================
// Socket Chat C++20 — 3D Hardware & Protocol Inspection Studio Engine
// High-Detail Multi-Component Mechanical & Electronic Cutaway Assemblies:
// - Procedural Studio HDRI Reflection Environment (PMREMGenerator)
// - Precision CAD Edge Contours (EdgesGeometry) on machined housings
// - Compound Hardware Builders: Helical Springs, Toothed Gears, Hydraulic Rams,
//   Multi-Fin Heatsinks, Populated PCBs, Scissor Lifts, Flanged Manifolds,
//   64-Blade Server Racks, 8-Bolt Bank Vaults, Optical Prisms & Quadcopters.
// ============================================================================

(function () {
  "use strict";

  function showFatal(msg) {
    let el = document.getElementById("fatal-overlay");
    if (!el) {
      el = document.createElement("div");
      el.id = "fatal-overlay";
      el.style.cssText = "position:fixed;left:50%;top:50%;transform:translate(-50%,-50%);z-index:99;" +
        "max-width:680px;padding:16px 20px;background:rgba(40,12,12,0.96);border:1px solid #ef4444;" +
        "border-radius:8px;color:#fecaca;font:12px ui-monospace,monospace;white-space:pre-wrap;";
      document.body.appendChild(el);
    }
    el.textContent = "3D Studio Error:\n" + msg;
  }

  if (typeof THREE === "undefined") {
    showFatal("three.min.js did not load. Keep index.html, three.min.js, codebase_data.js and world3d.js in the same folder.");
    return;
  }

  const DATA = window.SOCKET_CHAT_DATA;
  if (!DATA) {
    showFatal("codebase_data.js did not load (window.SOCKET_CHAT_DATA missing).");
    return;
  }

  // ==================== NORMALIZE GENERATED DATA ====================
  const KNOWN_PATHS = new Set(DATA.files.map(f => f.path));

  function parseRefs(text) {
    const out = [];
    const re = /([A-Za-z0-9_\-./]+\.(?:cpp|hpp|proto|txt)):(\d+)/g;
    let m;
    while ((m = re.exec(text || "")) !== null) {
      if (KNOWN_PATHS.has(m[1])) out.push([m[1], parseInt(m[2], 10)]);
    }
    return out;
  }

  DATA.flows = (DATA.flows || []).map(f => ({
    title: f.title || "",
    badge: f.badge || "",
    subtitle: f.summary || f.subtitle || "",
    steps: (f.steps || []).map(s => {
      const ref = s.code_ref || (s.file ? [s.file, s.line || 1] : null);
      return {
        actor: s.actor || "",
        target: s.target || s.title || "",
        packet: s.packet || s.wire || "",
        detail: s.detail || s.explain || "",
        code: s.code || "",
        code_ref: ref && KNOWN_PATHS.has(ref[0]) ? ref : null,
      };
    }),
  })).filter(f => f.steps.length > 0);

  DATA.requirements = (DATA.requirements || []).map(r => ({
    ...r,
    how: r.how || r.desc || "",
    defense: r.defense || "",
    serverRefs: r.server_ref ? [r.server_ref] : parseRefs(r.server_code),
    clientRefs: r.client_ref ? [r.client_ref] : parseRefs(r.client_code),
  }));

  DATA.defenseQa = (DATA.defenseQa || []).map(qa => ({
    ...qa,
    refs: qa.ref ? [qa.ref] : parseRefs((qa.q || "") + " " + (qa.a || "")),
  }));

  // ==================== 10 HARDWARE & PROTOCOL STATIONS ====================
  const ZONES_3D = [
    {
      id: "proto",
      num: "01",
      name: "Station 01: Wire Framing & Protobuf Codec",
      short: "01. Framing & Proto",
      x: -290, y: 0, z: 175,
      color: 0xf97316,
      hex: "#f97316",
      thread: "Pure Value / Codec Layer",
      subParts: [
        { idx: 0, label: "Envelope Oneof Hydraulic Die Press", code: "protocol_version = 1", dx: -34, dy: 20, dz: -18 },
        { idx: 1, label: "4-Byte Big-Endian Guillotine Conveyor", code: "(size >> 24/16/8) & 0xFF", dx: -10, dy: 18, dz: 18 },
        { idx: 2, label: "FrameDecoder 1MiB Caliper Gate", code: "header_[4] -> payload_", dx: 18, dy: 18, dz: 18 },
        { idx: 3, label: "RAND_bytes(32) 3-Axis Gyroscope", code: "new_message_key() 256-bit", dx: 34, dy: 24, dz: -18 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: 42B Frame", status: "Encoding [00,00,00,2A] + 42B Protobuf Payload" },
        { id: "split", label: "TCP Split 2B + Coalesced", status: "Holding at header_size_=2, then slicing 2 frames" },
        { id: "attack_4gb", label: "Fault: 4 GiB Header (0xFFFFFFFF)", status: "BLOCKED at 1 MiB Caliper Gate before reserve()!", danger: true },
        { id: "attack_eof", label: "Fault: Mid-Frame EOF", status: "REJECTED by finish(): header_size_ != 0 at EOF!", danger: true },
      ]
    },
    {
      id: "tls",
      num: "02",
      name: "Station 02: TLS 1.3 Airlock & X.509 SAN Scanner",
      short: "02. TLS 1.3 & SAN",
      x: -100, y: 0, z: 205,
      color: 0x10b981,
      hex: "#10b981",
      thread: "OpenSSL TLS 1.3 + X.509 SAN",
      subParts: [
        { idx: 0, label: "TLS 1.3 Octagonal Bulkhead Airlock", code: "TLS1_3_VERSION only", dx: -34, dy: 22, dz: -4 },
        { idx: 1, label: "X.509 SAN Optical Laser Gantry", code: "set1_ip_asc / SSL_set1_host", dx: -2, dy: 22, dz: 18 },
        { idx: 2, label: "Cert + Key Meshing Twin Gearbox", code: "SSL_CTX_check_private_key", dx: 10, dy: 18, dz: -20 },
        { idx: 3, label: "SocketStream Track Turntable", code: "tls_->async_* vs plain_", dx: 34, dy: 16, dz: 12 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: TLS 1.3 + IP SAN", status: "Verified CA + X.509 iPAddress SAN (127.0.0.1)" },
        { id: "dns_san", label: "Nominal: TLS 1.3 + DNS SAN", status: "Verified CA + X.509 dNSName SAN + SNI" },
        { id: "attack_tls12", label: "Fault: TLS 1.2 / Bad Cert", status: "BULKHEAD SEALED: TlsError (Terminal — No Retry)", danger: true },
        { id: "attack_lan_plain", label: "Fault: Non-Loopback Plaintext", status: "DERAILED: --plaintext-dev forbids LAN IP!", danger: true },
      ]
    },
    {
      id: "server",
      num: "03",
      name: "Station 03: PingServer 64-Blade Rack & Strand Monorail",
      short: "03. Server & Strand",
      x: 100, y: 0, z: 205,
      color: 0xf59e0b,
      hex: "#f59e0b",
      thread: "Shared Server Strand (executor_)",
      subParts: [
        { idx: 0, label: "64-Blade Server Rack & Fan Array", code: "hub_.size() >= 64 drop", dx: -34, dy: 26, dz: 12 },
        { idx: 1, label: "Single-File Strand Monorail", code: "make_strand(context)", dx: 0, dy: 16, dz: 4 },
        { idx: 2, label: "weak_ptr<Impl> Helical Spring Crane", code: "std::weak_ptr<Impl> owner", dx: 14, dy: 28, dz: -20 },
        { idx: 3, label: "Deferred asio::post Spiral Chute", code: "post(executor_, hub_.remove)", dx: 34, dy: 18, dz: 16 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: Strand Serialization", status: "Work-sleds execute strictly 1-by-1 on executor_" },
        { id: "attack_65", label: "Fault: 65th Connection Flood", status: "DEFLECTED by 64-Blade Rack Blast Shield!", danger: true },
        { id: "reentrant_post", label: "Overflow: Deferred Remove", status: "asio::post routes hub_.remove() to tail of monorail" },
      ]
    },
    {
      id: "session",
      num: "04",
      name: "Station 04: ClientSession Buffer Tank & Idle Chronometer",
      short: "04. ClientSession",
      x: 285, y: 0, z: 155,
      color: 0x38bdf8,
      hex: "#38bdf8",
      thread: "queue_mutex_ + Shared Strand",
      subParts: [
        { idx: 0, label: "256-Frame / 2MiB Sight-Glass Tank", code: "frame->size() > limit - queued", dx: -34, dy: 24, dz: -14 },
        { idx: 1, label: "Single async_write Crankshaft Pump", code: "shared_ptr<const vector> frame", dx: -6, dy: 16, dz: 18 },
        { idx: 2, label: "Half-Close Dual-Gate Valve Chamber", code: "read_closed_ = true; drain queue_", dx: 18, dy: 16, dz: -16 },
        { idx: 3, label: "30s Lazy Idle Gear Chronometer", code: "last_activity_ on valid Envelope", dx: 34, dy: 28, dz: 14 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: Burst -> Single Pump", status: "pump_scheduled_ coalesces 6 frames into 1 piston stroke" },
        { id: "attack_overflow", label: "Fault: Slow Client > 2 MiB", status: "TANK OVERPRESSURE: Abortive socket().close()!", danger: true },
        { id: "attack_slowloris", label: "Fault: 1-Byte Trickle Slowloris", status: "CHRONOMETER EXPIRED: Trickle bytes don't reset clock!", danger: true },
        { id: "half_close", label: "TCP Half-Close (EOF) Drain", status: "Inbound valve shut -> Draining outbound Pong before close" },
      ]
    },
    {
      id: "hub",
      num: "05",
      name: "Station 05: ChatHub Switchyard & R7 Privacy Blast Shield",
      short: "05. ChatHub Router",
      x: 295, y: 0, z: -105,
      color: 0xef4444,
      hex: "#ef4444",
      thread: "100% Shared Server Strand",
      subParts: [
        { idx: 0, label: "Anti-Spoofing Laser Turnstile Gate", code: "Reject client sender/room_id/seq", dx: -34, dy: 22, dz: 14 },
        { idx: 1, label: "usernames_ Commutator & R4 Radar", code: "usernames_ (R3) + broadcast (R4)", dx: -12, dy: 30, dz: -18 },
        { idx: 2, label: "3-Terminal DM Yard & R7 Guillotine", code: "Pair{min,max} + 0B to Charlie", dx: 14, dy: 18, dz: 18 },
        { idx: 3, label: "Voluntary Group Ring-Bus (R8-R11)", code: "members.contains(&session)", dx: 34, dy: 20, dz: -14 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: Private DM Alice->Bob", status: "Charlie's R7 Hydraulic Shield SEALED (0 bytes leaked)" },
        { id: "attack_spoof", label: "Fault: Spoof sender=\"Admin\"", status: "BLOCKED at Turnstile Gate: ERROR_CODE_INVALID_REQUEST!", danger: true },
        { id: "attack_nonmember", label: "Fault: Non-Member Group Send", status: "INTERLOCK LOCKED: ERROR_CODE_NOT_GROUP_MEMBER!", danger: true },
        { id: "group_flow", label: "Create Group (R8) + Join (R10)", status: "Creator-only {&session}, then explicit join by group_id" },
      ]
    },
    {
      id: "sqlite",
      num: "06",
      name: "Station 06: SQLite WAL Bank Vault & Magnetic Spindle",
      short: "06. SQLite WAL Vault",
      x: 105, y: 0, z: -195,
      color: 0xf97316,
      hex: "#f97316",
      thread: "Server Strand (WAL + FULL)",
      subParts: [
        { idx: 0, label: "BEGIN IMMEDIATE 8-Bolt Vault Door", code: "Transaction RAII + SQLITE_TRANSIENT", dx: -34, dy: 22, dz: 14 },
        { idx: 1, label: "EVP_sha256 Twin-Gear Crystal Mill", code: "token_hash = SHA256(resume_token)", dx: -8, dy: 24, dz: -18 },
        { idx: 2, label: "Schema v1->v2 Scissor Pantograph", code: "row_number() OVER + nullif(?,'')", dx: 14, dy: 22, dz: 18 },
        { idx: 3, label: "3-Platter WAL Spindle & Actuators", code: "sequence == next_sequence + 1", dx: 34, dy: 20, dz: -12 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: ACID Commit Before Ack", status: "BEGIN IMMEDIATE -> append(seq+1) -> COMMIT -> Ack" },
        { id: "retry_dedupe", label: "Idempotent Retry Key Hit", status: "Matched (principal, client_key) -> Returning saved Ack" },
        { id: "attack_conflict", label: "Fault: Reuse Key w/ New Text", status: "VAULT LOCKDOWN: MessageIdConflict rollback!", danger: true },
        { id: "attack_gap", label: "Fault: Corrupted Sequence Gap", status: "ABORTED: sequence != next_seq + 1 -> SYNC_GAP!", danger: true },
      ]
    },
    {
      id: "client",
      num: "07",
      name: "Station 07: PingClient Railgun & Optical Demux Prism",
      short: "07. PingClient SDK",
      x: -95, y: 0, z: -195,
      color: 0x60a5fa,
      hex: "#60a5fa",
      thread: "Caller Thread (Private io_context)",
      subParts: [
        { idx: 0, label: "3-Stage Connect Gantry (3.0s)", code: "resolve -> connect -> handshake", dx: -34, dy: 22, dz: -16 },
        { idx: 1, label: "Phase 1: run_one() 6-Coil Railgun", code: "while(!written) context_.run_one()", dx: -8, dy: 20, dz: 18 },
        { idx: 2, label: "Phase 2: Optical Beam-Splitter Prism", code: "request_id match vs events_ hopper", dx: 18, dy: 22, dz: 14 },
        { idx: 3, label: "Soft cancel() Disc Caliper Brake", code: "arm(timeout, soft=true)", dx: 34, dy: 18, dz: -16 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: Two-Phase Exchange", status: "Railgun fires request -> Prism routes matching Ack" },
        { id: "interleaved", label: "Interleaved Snapshot Before Ack", status: "Prism refracts UserSnapshot 90° into events_ magazine" },
        { id: "soft_poll", label: "Soft 25ms Poll Timeout", status: "Caliper feathers socket().cancel(); TLS stays alive" },
      ]
    },
    {
      id: "worker",
      num: "08",
      name: "Station 08: ChatWorker Drawbridge & Recovery Quadcopter",
      short: "08. ChatWorker",
      x: -280, y: 0, z: -140,
      color: 0xa78bfa,
      hex: "#a78bfa",
      thread: "std::jthread + mutex_ Queues",
      subParts: [
        { idx: 0, label: "submit() Hydraulic Key Coining Press", code: "Command.message_key minted upfront", dx: -34, dy: 22, dz: 14 },
        { idx: 1, label: "stop_token Helical Stasis Chamber", code: "wait_for(lock, stop, delay)", dx: -8, dy: 22, dz: -18 },
        { idx: 2, label: "Bascule Drawbridge & Quadcopter", code: "resume_login -> wanted_groups -> sync", dx: 14, dy: 26, dz: 12 },
        { idx: 3, label: "terminal_ Dual-Latch Signal Tower", code: "terminal_ set BEFORE running_=false", dx: 34, dy: 22, dz: -14 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: Command + 10s Heartbeat", status: "Drawbridge locked; jthread exchanges + polls (25ms)" },
        { id: "wifi_drop", label: "Fault: Drop Wi-Fi & Recover!", status: "Drawbridge opens -> Quadcopter flies key across -> Reconnects!", danger: true },
        { id: "seq_gap", label: "Live Sequence Gap (Seq 5 After 3)", status: "Detected gap -> Replaying missing Seq 4 before Seq 5" },
      ]
    },
    {
      id: "model",
      num: "09",
      name: "Station 09: RoomModel AS/RS Warehouse & Balance Scale",
      short: "09. RoomModel",
      x: -365, y: 0, z: -5,
      color: 0x818cf8,
      hex: "#818cf8",
      thread: "UI Thread (Pure C++20 State)",
      subParts: [
        { idx: 0, label: "Robotic Clamp & Welder Cell", code: "direct-view-1 -> binds direct-1", dx: -34, dy: 20, dz: -16 },
        { idx: 1, label: "Twin-Pan Analytical Balance Scale", code: "if (room.draft == submitted) clear()", dx: -6, dy: 26, dz: 18 },
        { idx: 2, label: "Mechanized Quarantine Airlock Bay", code: "room.available = false on leave", dx: 16, dy: 20, dz: -18 },
        { idx: 3, label: "3-Aisle AS/RS Rack & Stacker Crane", code: "Evicts oldest line; keeps sequence", dx: 32, dy: 24, dz: 14 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: Bind Room & Append", status: "Welds direct-view-1 to direct-1; advances last_sequence" },
        { id: "draft_save", label: "Type While Send In-Flight", status: "Scale unbalanced (draft != submitted) -> Preserves draft!" },
        { id: "namesake", label: "Bob Leaves & New 'Bob' Logs In", status: "Quarantine gate locks old room; new Bob gets fresh bay" },
      ]
    },
    {
      id: "ui",
      num: "10",
      name: "Station 10: CLI Terminal, Win32 Rig & CMake Pyramid",
      short: "10. CLI / GUI / CMake",
      x: -365, y: 0, z: 125,
      color: 0x34d399,
      hex: "#34d399",
      thread: "Main CLI / Win32 UI Thread",
      subParts: [
        { idx: 0, label: "Developer Cockpit & Surrogate Synth", code: "0xD800 + 0xDC00 -> 4B UTF-8 Emoji", dx: -34, dy: 22, dz: -14 },
        { idx: 1, label: "4-Byte UTF-8 Train & Parabolic Shield", code: "(byte & 0xc0)==0x80 + escape_text()", dx: -8, dy: 22, dz: 18 },
        { idx: 2, label: "Exploded 3D Win32 HWND Rig", code: "replace('\\0', L'␀') + WM_TIMER 40ms", dx: 16, dy: 24, dz: -14 },
        { idx: 3, label: "6-Tier CMake Busbar Ziggurat", code: "chat_proto -> framing -> transport", dx: 34, dy: 22, dz: 16 },
      ],
      triggers: [
        { id: "normal", label: "Nominal: UTF-16 Surrogate + Backspace", status: "Pairs surrogates -> Backspace pops all 4 UTF-8 bytes" },
        { id: "attack_ansi", label: "Fault: ANSI Escape (\\x1b[2J)", status: "DEFLECTED by Shield: Rendered safely as \"\\x1b[2J\"!", danger: true },
        { id: "attack_nul", label: "Fault: Embedded \\0 NUL in GUI", status: "FILTERED to L'␀' (U+2400): SetWindowTextW intact!", danger: true },
      ]
    }
  ];

  const HIGHWAY_LINKS = [
    ["ui", "model"],
    ["ui", "worker"],
    ["model", "worker"],
    ["worker", "client"],
    ["client", "proto"],
    ["proto", "tls"],
    ["tls", "server"],
    ["server", "session"],
    ["session", "hub"],
    ["hub", "sqlite"],
  ];

  function fileToZoneId(path) {
    if (!path) return "tls";
    if (path.startsWith("proto/") || path.includes("framing") || path.includes("message_identity")) return "proto";
    if (path.includes("transport/")) return "tls";
    if (path.includes("ping_server") || path.startsWith("apps/server/")) return "server";
    if (path.includes("client_session")) return "session";
    if (path.includes("chat_hub")) return "hub";
    if (path.includes("history_store")) return "sqlite";
    if (path.includes("ping_client") || path.includes("chat_client")) return "client";
    if (path.includes("chat_worker")) return "worker";
    if (path.includes("room_model")) return "model";
    return "ui";
  }

  function fileBlockToSubPartIdx(path, blockIdx) {
    if (!path) return 0;
    if (path === "proto/chat.proto") return 0;
    if (path.includes("framing")) return blockIdx === 0 ? 1 : 2;
    if (path.includes("message_identity")) return 3;
    if (path.includes("tls.cpp")) return blockIdx === 0 ? 0 : 1;
    if (path.includes("socket_stream")) return 3;
    if (path.includes("ping_server")) return blockIdx === 0 ? 1 : 3;
    if (path.includes("client_session")) return Math.min(3, blockIdx);
    if (path.includes("chat_hub")) return Math.min(3, Math.floor(blockIdx * 0.7));
    if (path.includes("history_store")) return Math.min(3, blockIdx);
    if (path.includes("ping_client")) return Math.min(3, Math.floor(blockIdx * 0.7));
    if (path.includes("chat_worker")) return Math.min(3, blockIdx);
    if (path.includes("room_model")) return Math.min(3, blockIdx);
    if (path.includes("console_input")) return 0;
    if (path.includes("chat_console")) return 1;
    if (path.includes("gui/")) return 2;
    if (path === "CMakeLists.txt") return 3;
    return blockIdx % 4;
  }

  // ==================== BUILD SCENES & STEP PLAYLISTS ====================
  const SCENES = [];

  DATA.flows.forEach((flow, fIdx) => {
    const firstRef = flow.steps.find(s => s.code_ref) || { code_ref: ["src/server/chat_hub.cpp", 231] };
    const refFileIdx = DATA.files.findIndex(f => f.path === firstRef.code_ref[0]);
    SCENES.push({
      sceneIndex: SCENES.length,
      kind: "flow",
      actLabel: `ACT I • MISSION ${fIdx + 1}/8`,
      flowIndex: fIdx,
      flow: flow,
      title: `Mission ${fIdx + 1}: ${flow.title}`,
      subtitle: flow.subtitle,
      fileIndex: refFileIdx >= 0 ? refFileIdx : 0,
      startLine: firstRef.code_ref[1],
      endLine: Math.min((DATA.files[refFileIdx]?.lineCount || 30), firstRef.code_ref[1] + 15),
      zoneId: fileToZoneId(firstRef.code_ref[0]),
      subPartIdx: 0,
    });
  });

  DATA.files.forEach((fileObj, fIdx) => {
    const zId = fileToZoneId(fileObj.path);
    fileObj.blocks.forEach((block, bIdx) => {
      SCENES.push({
        sceneIndex: SCENES.length,
        kind: "code_block",
        actLabel: `ACT II • ${fileObj.module.toUpperCase()}`,
        fileIndex: fIdx,
        blockIndex: bIdx,
        file: fileObj,
        block: block,
        title: `${fileObj.path} — ${block.name}`,
        subtitle: block.summary,
        startLine: block.lines[0],
        endLine: block.lines[1],
        zoneId: zId,
        subPartIdx: fileBlockToSubPartIdx(fileObj.path, bIdx),
      });
    });
  });

  function buildPlaylist(mode) {
    const list = [];
    SCENES.forEach((sc, sIdx) => {
      if (sc.kind === "flow") {
        if (mode === "scene") {
          list.push({ sceneIndex: sIdx, flowStepIndex: 0, lineNum: sc.startLine });
        } else {
          sc.flow.steps.forEach((st, stIdx) => {
            const refPath = st.code_ref ? st.code_ref[0] : sc.file?.path;
            const refLine = st.code_ref ? st.code_ref[1] : sc.startLine;
            const fIdx = DATA.files.findIndex(f => f.path === refPath);
            list.push({
              sceneIndex: sIdx,
              flowStepIndex: stIdx,
              overrideFileIndex: fIdx >= 0 ? fIdx : sc.fileIndex,
              overrideZoneId: fileToZoneId(refPath),
              overrideSubIdx: fileBlockToSubPartIdx(refPath, stIdx),
              lineNum: refLine,
            });
          });
        }
      } else {
        const fObj = sc.file;
        const startL = sc.startLine;
        const endL = sc.endLine;
        const pinnedInBlock = [];
        for (let ln = startL; ln <= endL; ln++) {
          const item = fObj.lines[ln - 1];
          if (item && item[5] === 1) pinnedInBlock.push(ln);
        }
        const defaultLine = pinnedInBlock.length > 0 ? pinnedInBlock[0] : startL;

        if (mode === "scene") {
          list.push({ sceneIndex: sIdx, lineNum: defaultLine });
        } else if (mode === "smart") {
          list.push({ sceneIndex: sIdx, lineNum: defaultLine });
          pinnedInBlock.forEach((pLn, idx) => {
            if (idx > 0 || pLn !== defaultLine) {
              list.push({ sceneIndex: sIdx, lineNum: pLn });
            }
          });
        } else {
          for (let ln = startL; ln <= endL; ln++) {
            list.push({ sceneIndex: sIdx, lineNum: ln });
          }
        }
      }
    });
    return list;
  }

  const PLAYLISTS = {
    smart: buildPlaylist("smart"),
    scene: buildPlaylist("scene"),
    all_lines: buildPlaylist("all_lines"),
  };

  // ==================== RUNTIME STATE ====================
  let currentMode = "smart";
  const firstCodeBlockStep = PLAYLISTS.smart.findIndex(it => SCENES[it.sceneIndex].kind === "code_block" && SCENES[it.sceneIndex].zoneId === "proto");
  let currentStepIdx = firstCodeBlockStep >= 0 ? firstCodeBlockStep : 0;
  if (location.hash.includes("act1")) {
    currentStepIdx = 0;
  } else {
    const zMatch = location.hash.match(/zone=([a-z]+)/);
    if (zMatch) {
      const zIdx = PLAYLISTS.smart.findIndex(it => SCENES[it.sceneIndex].kind === "code_block" && SCENES[it.sceneIndex].zoneId === zMatch[1]);
      if (zIdx >= 0) currentStepIdx = zIdx;
    }
  }

  let activeLineOverride = null;
  let activeSimMode = "normal";
  let autoPlayTimer = null;
  let hudExpanded = false;
  let hudHidden = false;

  // Camera state
  let cameraLocked = true;
  let closeUpSubAssembly = false;
  let orbitYaw = 0.08;
  let orbitPitch = 0.46;
  let orbitDist = 114;
  let keysPressed = {};
  let isDragging = false;
  let dragPrev = { x: 0, y: 0 };

  function escapeHtml(str) {
    return String(str)
      .replace(/&/g, "&amp;")
      .replace(/</g, "&lt;")
      .replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;");
  }

  function formatInlineCode(text) {
    if (!text) return "";
    return escapeHtml(text)
      .replace(/\*\*([^*]+)\*\*/g, "<strong>$1</strong>")
      .replace(/`([^`]+)`/g, '<code class="code-pill">$1</code>');
  }

  function getCurrentStep() {
    const pl = PLAYLISTS[currentMode];
    if (currentStepIdx < 0) currentStepIdx = 0;
    if (currentStepIdx >= pl.length) currentStepIdx = pl.length - 1;
    return pl[currentStepIdx];
  }

  function getActiveZoneAndSubPart() {
    const step = getCurrentStep();
    const sc = SCENES[step.sceneIndex];
    const zId = step.overrideZoneId || sc.zoneId || "proto";
    const zone = ZONES_3D.find(z => z.id === zId) || ZONES_3D[0];
    const subIdx = step.overrideSubIdx !== undefined
      ? step.overrideSubIdx
      : (sc.kind === "flow" ? (step.flowStepIndex || 0) % 4 : (sc.subPartIdx || 0));
    const subPart = zone.subParts[subIdx] || zone.subParts[0];
    return { step, sc, zone, subPart, subIdx };
  }

  // ==================== THREE.JS STUDIO & PROCEDURAL HDRI ENVIRONMENT ====================
  const worldCanvas = document.getElementById("world-canvas");
  let renderer;
  try {
    renderer = new THREE.WebGLRenderer({
      canvas: worldCanvas,
      antialias: true,
      powerPreference: "high-performance"
    });
  } catch (e) {
    showFatal("WebGL could not start in this browser.\n" + e.message);
    return;
  }
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.25;

  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x121417);
  scene.fog = new THREE.FogExp2(0x121417, 0.00072);

  try {
    const pmrem = new THREE.PMREMGenerator(renderer);
    pmrem.compileCubemapShader();
    const envScene = new THREE.Scene();
    envScene.background = new THREE.Color(0x181b20);

    const softboxTop = new THREE.Mesh(
      new THREE.PlaneGeometry(130, 130),
      new THREE.MeshBasicMaterial({ color: 0xfff7ed, side: THREE.DoubleSide })
    );
    softboxTop.position.set(0, 75, 0);
    softboxTop.rotation.x = Math.PI / 2;
    envScene.add(softboxTop);

    const keyPanel = new THREE.Mesh(
      new THREE.PlaneGeometry(95, 65),
      new THREE.MeshBasicMaterial({ color: 0xffedd5, side: THREE.DoubleSide })
    );
    keyPanel.position.set(80, 28, 65);
    keyPanel.lookAt(0, 0, 0);
    envScene.add(keyPanel);

    const rimPanel = new THREE.Mesh(
      new THREE.PlaneGeometry(100, 65),
      new THREE.MeshBasicMaterial({ color: 0x93c5fd, side: THREE.DoubleSide })
    );
    rimPanel.position.set(-85, 32, -65);
    rimPanel.lookAt(0, 0, 0);
    envScene.add(rimPanel);

    scene.environment = pmrem.fromScene(envScene, 0.04).texture;
    pmrem.dispose();
  } catch (envErr) {
    console.warn("PMREM fallback:", envErr);
  }

  const camera = new THREE.PerspectiveCamera(42, window.innerWidth / window.innerHeight, 1, 2600);
  const camCurrentPos = new THREE.Vector3();
  const camTargetPos = new THREE.Vector3();
  const camCurrentLook = new THREE.Vector3();
  const camTargetLook = new THREE.Vector3();

  function snapCameraToActive() {
    const { zone, subPart } = getActiveZoneAndSubPart();
    const fw = closeUpSubAssembly ? 0.90 : 0.0;
    const dist = closeUpSubAssembly ? 56 : orbitDist;
    const fx = zone.x + subPart.dx * fw;
    const fy = closeUpSubAssembly ? subPart.dy * 0.7 : 12;
    const fz = zone.z + subPart.dz * fw;
    camTargetLook.set(fx, fy, fz);
    camTargetPos.set(
      fx + Math.sin(orbitYaw) * dist,
      fy + Math.sin(orbitPitch) * dist,
      fz + Math.cos(orbitYaw) * dist
    );
    camCurrentLook.copy(camTargetLook);
    camCurrentPos.copy(camTargetPos);
    camera.position.copy(camCurrentPos);
    camera.lookAt(camCurrentLook);
  }

  function updateCameraFrustumOffset() {
    const w = window.innerWidth;
    const h = window.innerHeight;
    camera.aspect = w / h;
    // Center the 3D station inside the open viewport between Left Dock (262px) and Right Inspector (414px)
    const offsetX = hudHidden ? -80 : (hudExpanded ? 140 : 68);
    const offsetY = 24;
    camera.setViewOffset(w, h, offsetX, offsetY, w, h);
  }
  snapCameraToActive();
  updateCameraFrustumOffset();

  // Studio Three-Point Lighting
  const hemiLight = new THREE.HemisphereLight(0xf3f4f6, 0x1f242d, 1.0);
  scene.add(hemiLight);

  const keyLight = new THREE.DirectionalLight(0xfffbeb, 1.6);
  keyLight.position.set(210, 360, 240);
  keyLight.castShadow = true;
  keyLight.shadow.mapSize.width = 2048;
  keyLight.shadow.mapSize.height = 2048;
  keyLight.shadow.camera.near = 30;
  keyLight.shadow.camera.far = 1200;
  const shadowBounds = 480;
  keyLight.shadow.camera.left = -shadowBounds;
  keyLight.shadow.camera.right = shadowBounds;
  keyLight.shadow.camera.top = shadowBounds;
  keyLight.shadow.camera.bottom = -shadowBounds;
  keyLight.shadow.bias = -0.0005;
  scene.add(keyLight);

  const rimLight = new THREE.DirectionalLight(0x93c5fd, 0.7);
  rimLight.position.set(-240, 190, -210);
  scene.add(rimLight);

  const activePointLight = new THREE.PointLight(0xf97316, 2.5, 160);
  scene.add(activePointLight);

  // Studio Test-Floor & Precision CAD Reference Grid
  const floorMesh = new THREE.Mesh(
    new THREE.PlaneGeometry(2000, 2000),
    new THREE.MeshStandardMaterial({ color: 0x121417, roughness: 0.78, metalness: 0.22 })
  );
  floorMesh.rotation.x = -Math.PI / 2;
  floorMesh.position.y = -0.2;
  floorMesh.receiveShadow = true;
  scene.add(floorMesh);

  const majorGrid = new THREE.GridHelper(1300, 52, 0x2c313c, 0x1b1e25);
  majorGrid.position.y = 0.02;
  scene.add(majorGrid);

  // ==================== INDUSTRIAL PBR MATERIALS & COMPOUND HARDWARE BUILDERS ====================
  const MAT = {
    darkIron: new THREE.MeshStandardMaterial({ color: 0x1d2027, metalness: 0.74, roughness: 0.40 }),
    slateDeck: new THREE.MeshStandardMaterial({ color: 0x252a34, metalness: 0.58, roughness: 0.45 }),
    anodizedBlack: new THREE.MeshStandardMaterial({ color: 0x14171d, metalness: 0.80, roughness: 0.30 }),
    brushedSteel: new THREE.MeshStandardMaterial({ color: 0xcbd5e1, metalness: 0.88, roughness: 0.22 }),
    chrome: new THREE.MeshStandardMaterial({ color: 0xf8fafc, metalness: 0.96, roughness: 0.10 }),
    titanium: new THREE.MeshStandardMaterial({ color: 0x8b94a3, metalness: 0.82, roughness: 0.28 }),
    copper: new THREE.MeshStandardMaterial({ color: 0xd97742, metalness: 0.90, roughness: 0.20 }),
    brass: new THREE.MeshStandardMaterial({ color: 0xdba944, metalness: 0.88, roughness: 0.22 }),
    pcbGreen: new THREE.MeshStandardMaterial({ color: 0x0f4c3a, metalness: 0.32, roughness: 0.38 }),
    carbonFiber: new THREE.MeshStandardMaterial({ color: 0x1e222a, metalness: 0.48, roughness: 0.34 }),
    glass: new THREE.MeshStandardMaterial({ color: 0xdbeafe, transparent: true, opacity: 0.28, roughness: 0.08, metalness: 0.15 }),
    signalOrange: new THREE.MeshStandardMaterial({ color: 0xf97316, emissive: 0xea580c, emissiveIntensity: 0.32, roughness: 0.26, metalness: 0.35 }),
    nominalEmerald: new THREE.MeshStandardMaterial({ color: 0x10b981, emissive: 0x059669, emissiveIntensity: 0.42, roughness: 0.24, metalness: 0.3 }),
    hazardRed: new THREE.MeshStandardMaterial({ color: 0xef4444, emissive: 0xdc2626, emissiveIntensity: 0.55, roughness: 0.24, metalness: 0.3 }),
    amberGold: new THREE.MeshStandardMaterial({ color: 0xf59e0b, emissive: 0xd97706, emissiveIntensity: 0.38, roughness: 0.22, metalness: 0.68 }),
    laserBlue: new THREE.MeshStandardMaterial({ color: 0x38bdf8, emissive: 0x0284c7, emissiveIntensity: 0.48, roughness: 0.18, metalness: 0.35 }),
    violetLogic: new THREE.MeshStandardMaterial({ color: 0xa78bfa, emissive: 0x7c3aed, emissiveIntensity: 0.38, roughness: 0.26, metalness: 0.45 }),
  };

  const CAD_EDGE_DARK = new THREE.LineBasicMaterial({ color: 0x090b0e, transparent: true, opacity: 0.55 });
  const CAD_EDGE_LIGHT = new THREE.LineBasicMaterial({ color: 0x94a3b8, transparent: true, opacity: 0.34 });

  const clickableMeshes = [];
  const globalSpinFans = [];

  function cadMesh(geo, mat, zoneId, subIdx, lightEdges = false) {
    const mesh = new THREE.Mesh(geo, mat);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    if (zoneId !== undefined) {
      mesh.userData = { zoneId, subIdx: subIdx ?? 0 };
      clickableMeshes.push(mesh);
    }
    const edges = new THREE.EdgesGeometry(geo, 22);
    const lines = new THREE.LineSegments(edges, lightEdges ? CAD_EDGE_LIGHT : CAD_EDGE_DARK);
    mesh.add(lines);
    return mesh;
  }

  function makeGear(outerR, innerR, thickness, teeth, mat, zoneId, subIdx) {
    const shape = new THREE.Shape();
    const step = (Math.PI * 2) / teeth;
    for (let i = 0; i < teeth; i++) {
      const a0 = i * step, a1 = a0 + step * 0.22, a2 = a0 + step * 0.48, a3 = a0 + step * 0.70;
      const p0x = Math.cos(a0) * innerR, p0y = Math.sin(a0) * innerR;
      const p1x = Math.cos(a1) * outerR, p1y = Math.sin(a1) * outerR;
      const p2x = Math.cos(a2) * outerR, p2y = Math.sin(a2) * outerR;
      const p3x = Math.cos(a3) * innerR, p3y = Math.sin(a3) * innerR;
      if (i === 0) shape.moveTo(p0x, p0y); else shape.lineTo(p0x, p0y);
      shape.lineTo(p1x, p1y); shape.lineTo(p2x, p2y); shape.lineTo(p3x, p3y);
    }
    shape.closePath();
    const hole = new THREE.Path();
    hole.absarc(0, 0, innerR * 0.36, 0, Math.PI * 2, false);
    shape.holes.push(hole);
    const geo = new THREE.ExtrudeGeometry(shape, { depth: thickness, bevelEnabled: true, bevelThickness: 0.22, bevelSize: 0.18, bevelSegments: 1 });
    geo.center();
    return cadMesh(geo, mat, zoneId, subIdx, true);
  }

  function makeHelicalSpring(radius, height, turns, wireR, mat, zoneId, subIdx) {
    const pts = [];
    const steps = turns * 14;
    for (let i = 0; i <= steps; i++) {
      const u = i / steps;
      const ang = u * turns * Math.PI * 2;
      pts.push(new THREE.Vector3(Math.cos(ang) * radius, u * height, Math.sin(ang) * radius));
    }
    const curve = new THREE.CatmullRomCurve3(pts);
    const geo = new THREE.TubeGeometry(curve, steps, wireR, 8, false);
    const mesh = new THREE.Mesh(geo, mat);
    mesh.castShadow = true;
    if (zoneId !== undefined) {
      mesh.userData = { zoneId, subIdx };
      clickableMeshes.push(mesh);
    }
    return mesh;
  }

  function makeHeatsink(w, h, d, fins, mat, zoneId, subIdx) {
    const grp = new THREE.Group();
    const base = cadMesh(new THREE.BoxGeometry(w, h * 0.28, d), mat, zoneId, subIdx, true);
    base.position.y = h * 0.14;
    grp.add(base);
    const finW = (w / fins) * 0.42;
    for (let i = 0; i < fins; i++) {
      const fx = -w * 0.46 + (i / Math.max(1, fins - 1)) * (w * 0.92);
      const fin = new THREE.Mesh(new THREE.BoxGeometry(finW, h * 0.72, d * 0.96), mat);
      fin.position.set(fx, h * 0.64, 0);
      grp.add(fin);
    }
    return grp;
  }

  function makePopulatedPCB(w, d, zoneId, subIdx) {
    const grp = new THREE.Group();
    const board = cadMesh(new THREE.BoxGeometry(w, 0.9, d), MAT.pcbGreen, zoneId, subIdx, true);
    board.position.y = 0.45;
    grp.add(board);

    for (let i = 0; i < 8; i++) {
      const pin = new THREE.Mesh(new THREE.BoxGeometry(w * 0.07, 1.0, 1.1), MAT.brass);
      pin.position.set(-w * 0.38 + i * (w * 0.108), 0.5, d * 0.48);
      grp.add(pin);
    }

    const cpuPkg = cadMesh(new THREE.BoxGeometry(w * 0.38, 1.2, d * 0.38), MAT.anodizedBlack, zoneId, subIdx, true);
    cpuPkg.position.set(-w * 0.15, 1.5, 0);
    const hs = makeHeatsink(w * 0.32, 2.6, d * 0.32, 6, MAT.brushedSteel, zoneId, subIdx);
    hs.position.set(-w * 0.15, 2.1, 0);
    grp.add(cpuPkg, hs);

    for (let c = 0; c < 4; c++) {
      const cap = new THREE.Mesh(new THREE.CylinderGeometry(0.95, 0.95, 2.8, 12), MAT.brushedSteel);
      cap.position.set(w * 0.25, 2.2, -d * 0.32 + c * (d * 0.21));
      grp.add(cap);
    }

    const toroid = new THREE.Mesh(new THREE.TorusGeometry(1.6, 0.55, 8, 16), MAT.copper);
    toroid.position.set(w * 0.12, 2.2, d * 0.22);
    const led = new THREE.Mesh(new THREE.BoxGeometry(1.0, 0.8, 1.0), MAT.nominalEmerald);
    led.position.set(-w * 0.38, 1.2, -d * 0.35);
    grp.add(toroid, led);
    return grp;
  }

  function makeCoolingFan(radius, depth, zoneId, subIdx) {
    const grp = new THREE.Group();
    const shroud = cadMesh(new THREE.BoxGeometry(radius * 2.3, radius * 2.3, depth), MAT.anodizedBlack, zoneId, subIdx, true);
    const ring = new THREE.Mesh(new THREE.TorusGeometry(radius * 0.95, 0.35, 8, 20), MAT.titanium);
    ring.position.z = depth * 0.52;
    const rotor = new THREE.Group();
    const hub = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.32, radius * 0.32, depth * 0.8, 12), MAT.brushedSteel);
    hub.rotation.x = Math.PI / 2;
    rotor.add(hub);
    for (let b = 0; b < 7; b++) {
      const ang = (b * Math.PI * 2) / 7;
      const blade = new THREE.Mesh(new THREE.BoxGeometry(radius * 0.85, radius * 0.24, 0.25), MAT.signalOrange);
      blade.position.set(Math.cos(ang) * radius * 0.52, Math.sin(ang) * radius * 0.52, 0);
      blade.rotation.z = ang + 0.35;
      blade.rotation.y = 0.42;
      rotor.add(blade);
    }
    grp.add(shroud, ring, rotor);
    globalSpinFans.push(rotor);
    return grp;
  }

  function makeBoltedFlange(radius, thickness, mat, zoneId, subIdx) {
    const grp = new THREE.Group();
    const plate = cadMesh(new THREE.CylinderGeometry(radius, radius, thickness, 20), mat, zoneId, subIdx, true);
    grp.add(plate);
    for (let i = 0; i < 8; i++) {
      const a = (i * Math.PI) / 4;
      const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 0.42, thickness + 0.6, 6), MAT.chrome);
      bolt.position.set(Math.cos(a) * (radius * 0.82), 0, Math.sin(a) * (radius * 0.82));
      grp.add(bolt);
    }
    return grp;
  }

  function makeScissorLift(w, d, h, zoneId, subIdx) {
    const grp = new THREE.Group();
    const base = cadMesh(new THREE.BoxGeometry(w, 1.8, d), MAT.darkIron, zoneId, subIdx, true);
    base.position.y = 0.9;
    grp.add(base);
    [-d * 0.38, d * 0.38].forEach(zOff => {
      const len = Math.hypot(w * 0.82, h);
      const ang = Math.atan2(h, w * 0.82);
      const arm1 = cadMesh(new THREE.BoxGeometry(len, 1.1, 0.8), MAT.signalOrange, zoneId, subIdx, true);
      arm1.position.set(0, h * 0.5 + 1.5, zOff);
      arm1.rotation.z = ang;
      const arm2 = cadMesh(new THREE.BoxGeometry(len, 1.1, 0.8), MAT.brushedSteel, zoneId, subIdx, true);
      arm2.position.set(0, h * 0.5 + 1.5, zOff + (zOff > 0 ? -0.85 : 0.85));
      arm2.rotation.z = -ang;
      grp.add(arm1, arm2);
    });
    const topDeck = cadMesh(new THREE.BoxGeometry(w, 1.8, d), MAT.titanium, zoneId, subIdx, true);
    topDeck.position.y = h + 1.8;
    grp.add(topDeck);
    return grp;
  }

  function makeDiagnosticScreen(w, h, drawFn, zoneId, subIdx) {
    const grp = new THREE.Group();
    const bezel = cadMesh(new THREE.BoxGeometry(w + 1.4, h + 1.4, 1.1), MAT.anodizedBlack, zoneId, subIdx, true);
    grp.add(bezel);

    const canvas = document.createElement("canvas");
    canvas.width = 512;
    canvas.height = Math.round(512 * (h / w));
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#0b0e13";
    ctx.fillRect(0, 0, canvas.width, canvas.height);
    ctx.strokeStyle = "rgba(255,255,255,0.22)";
    ctx.lineWidth = 4;
    ctx.strokeRect(4, 4, canvas.width - 8, canvas.height - 8);
    if (drawFn) drawFn(ctx, canvas.width, canvas.height);

    const tex = new THREE.CanvasTexture(canvas);
    const screenMesh = new THREE.Mesh(
      new THREE.PlaneGeometry(w, h),
      new THREE.MeshBasicMaterial({ map: tex })
    );
    screenMesh.position.z = 0.62;
    screenMesh.userData = { zoneId, subIdx };
    clickableMeshes.push(screenMesh);
    grp.add(screenMesh);
    return grp;
  }

  function makeDataCartridge(w, h, d, topText, subText, bgHex, accentHex, zoneId, subIdx) {
    const grp = new THREE.Group();
    const body = cadMesh(new THREE.BoxGeometry(w, h, d), MAT.anodizedBlack, zoneId, subIdx, true);
    const ribL = new THREE.Mesh(new THREE.BoxGeometry(w * 1.04, h * 0.45, d * 0.82), MAT.titanium);
    const goldPins = new THREE.Mesh(new THREE.BoxGeometry(w * 0.75, h * 0.18, d * 1.04), MAT.brass);
    goldPins.position.y = -h * 0.35;
    grp.add(body, ribL, goldPins);

    const c = document.createElement("canvas");
    c.width = 256; c.height = 160;
    const g = c.getContext("2d");
    g.fillStyle = bgHex; g.fillRect(0, 0, 256, 160);
    g.strokeStyle = accentHex; g.lineWidth = 8; g.strokeRect(6, 6, 244, 148);
    g.fillStyle = "#ffffff"; g.font = "bold 38px Consolas, monospace"; g.textAlign = "center";
    g.fillText(topText, 128, 72);
    if (subText) {
      g.fillStyle = accentHex; g.font = "bold 24px Consolas, monospace";
      g.fillText(subText, 128, 122);
    }
    const tex = new THREE.CanvasTexture(c);
    const plateMat = new THREE.MeshBasicMaterial({ map: tex });
    const frontPlate = new THREE.Mesh(new THREE.PlaneGeometry(w * 0.88, h * 0.78), plateMat);
    frontPlate.position.z = d * 0.51;
    const topPlate = new THREE.Mesh(new THREE.PlaneGeometry(w * 0.88, d * 0.78), plateMat);
    topPlate.rotation.x = -Math.PI / 2;
    topPlate.position.y = h * 0.51;
    grp.add(frontPlate, topPlate);
    return grp;
  }

  function makeConveyorBed(len, wid, numRollers, zoneId, subIdx) {
    const grp = new THREE.Group();
    const railL = cadMesh(new THREE.BoxGeometry(len, 2.4, 1.3), MAT.titanium, zoneId, subIdx, true);
    railL.position.set(0, 1.8, wid * 0.5);
    const railR = cadMesh(new THREE.BoxGeometry(len, 2.4, 1.3), MAT.titanium, zoneId, subIdx, true);
    railR.position.set(0, 1.8, -wid * 0.5);
    grp.add(railL, railR);

    for (let s = -1; s <= 1; s += 2) {
      const leg1 = new THREE.Mesh(new THREE.BoxGeometry(1.4, 2.2, 1.4), MAT.darkIron);
      leg1.position.set(-len * 0.42, 1.1, s * wid * 0.5);
      const leg2 = new THREE.Mesh(new THREE.BoxGeometry(1.4, 2.2, 1.4), MAT.darkIron);
      leg2.position.set(len * 0.42, 1.1, s * wid * 0.5);
      grp.add(leg1, leg2);
    }

    for (let i = 0; i < numRollers; i++) {
      const rx = -len * 0.45 + (i / Math.max(1, numRollers - 1)) * (len * 0.9);
      const roller = new THREE.Mesh(new THREE.CylinderGeometry(0.85, 0.85, wid - 1.2, 14), MAT.brushedSteel);
      roller.rotation.x = Math.PI / 2;
      roller.position.set(rx, 2.0, 0);
      grp.add(roller);
    }
    return grp;
  }

  function makeInternalManifoldPipes(zone) {
    const grp = new THREE.Group();
    for (let i = 0; i < zone.subParts.length - 1; i++) {
      const a = zone.subParts[i];
      const b = zone.subParts[i + 1];
      const midX = (a.dx + b.dx) * 0.5;
      const curve = new THREE.CatmullRomCurve3([
        new THREE.Vector3(a.dx, 4.1, a.dz),
        new THREE.Vector3(midX, 4.1, a.dz),
        new THREE.Vector3(midX, 4.1, b.dz),
        new THREE.Vector3(b.dx, 4.1, b.dz),
      ]);
      const pipe = new THREE.Mesh(new THREE.TubeGeometry(curve, 20, 0.55, 8, false), MAT.copper);
      grp.add(pipe);
    }
    return grp;
  }

  function makeStationDeck(zone) {
    const grp = new THREE.Group();

    const chassis = cadMesh(new THREE.BoxGeometry(108, 3.2, 80), MAT.darkIron, zone.id, 0, true);
    chassis.position.y = 1.6;
    grp.add(chassis);

    [[-52, -38], [52, -38], [-52, 38], [52, 38]].forEach(([fx, fz]) => {
      const foot = cadMesh(new THREE.BoxGeometry(5.5, 3.8, 5.5), MAT.titanium, zone.id, 0, true);
      foot.position.set(fx, 1.9, fz);
      const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 0.9, 4.6, 8), MAT.chrome);
      bolt.position.set(fx, 2.3, fz);
      grp.add(foot, bolt);
    });

    const c = document.createElement("canvas");
    c.width = 1024;
    c.height = 768;
    const g = c.getContext("2d");
    g.fillStyle = "#181b22";
    g.fillRect(0, 0, 1024, 768);

    g.strokeStyle = "rgba(255, 255, 255, 0.045)";
    g.lineWidth = 1;
    for (let x = 0; x <= 1024; x += 32) { g.beginPath(); g.moveTo(x, 0); g.lineTo(x, 768); g.stroke(); }
    for (let y = 0; y <= 768; y += 32) { g.beginPath(); g.moveTo(0, y); g.lineTo(1024, y); g.stroke(); }

    g.strokeStyle = zone.hex;
    g.lineWidth = 5;
    g.strokeRect(18, 18, 988, 732);

    g.fillStyle = "rgba(255,255,255,0.88)";
    g.font = "bold 26px Consolas, monospace";
    g.fillText(`STATION ${zone.num} // ${zone.name.toUpperCase()}`, 40, 56);
    g.fillStyle = zone.hex;
    g.font = "bold 19px Consolas, monospace";
    g.fillText(`CONTEXT: ${zone.thread.toUpperCase()}`, 40, 84);

    const mapX = dx => 512 + (dx / 54) * 480;
    const mapZ = dz => 384 + (dz / 40) * 350;

    zone.subParts.forEach((sp, idx) => {
      const px = mapX(sp.dx), pz = mapZ(sp.dz);
      g.fillStyle = "rgba(10, 12, 16, 0.88)";
      g.strokeStyle = "rgba(255, 255, 255, 0.24)";
      g.lineWidth = 2.5;
      g.fillRect(px - 112, pz - 76, 224, 152);
      g.strokeRect(px - 112, pz - 76, 224, 152);
      g.fillStyle = zone.hex;
      g.font = "bold 20px Consolas, monospace";
      g.fillText(`[0${idx + 1}] ${sp.label.slice(0, 22).toUpperCase()}`, px - 102, pz + 64);
    });

    const deckTex = new THREE.CanvasTexture(c);
    const deckTop = new THREE.Mesh(
      new THREE.PlaneGeometry(106, 78),
      new THREE.MeshStandardMaterial({ map: deckTex, roughness: 0.55, metalness: 0.25 })
    );
    deckTop.rotation.x = -Math.PI / 2;
    deckTop.position.y = 3.25;
    deckTop.receiveShadow = true;
    grp.add(deckTop);

    grp.add(makeInternalManifoldPipes(zone));
    return grp;
  }
  // ==================== BUILD ALL 10 DETAILED 3D HARDWARE STATIONS ====================
  const machineGroups = {};

  ZONES_3D.forEach((zone) => {
    const group = new THREE.Group();
    group.position.set(zone.x, 0, zone.z);
    group.userData = { zoneId: zone.id, animRefs: {} };
    const refs = group.userData.animRefs;

    group.add(makeStationDeck(zone));

    const frontRail = cadMesh(new THREE.BoxGeometry(106, 0.9, 1.4), MAT.signalOrange.clone(), zone.id, 0);
    frontRail.material.color.setHex(zone.color);
    frontRail.material.emissive.setHex(zone.color);
    frontRail.position.set(0, 3.4, 39.5);
    group.add(frontRail);
    refs.rimMat = frontRail.material;

    // ==================== ZONE 01: WIRE FRAMING & PROTOBUF CODEC ====================
    if (zone.id === "proto") {
      // Sub 0: 4-Column Hydraulic Envelope Oneof Stamp Press + Populated PCB Die Bed (dx: -34, dz: -18)
      const pressGrp = new THREE.Group();
      pressGrp.position.set(-34, 3.3, -18);
      pressGrp.add(cadMesh(new THREE.BoxGeometry(22, 2.8, 17), MAT.darkIron, zone.id, 0, true));
      const pcbBed = makePopulatedPCB(16, 12, zone.id, 0);
      pcbBed.position.y = 1.6;
      pressGrp.add(pcbBed);
      [[-8.5, -6.5], [8.5, -6.5], [-8.5, 6.5], [8.5, 6.5]].forEach(([cx, cz], idx) => {
        const col = new THREE.Mesh(new THREE.CylinderGeometry(0.85, 0.85, 25, 14), MAT.chrome);
        col.position.set(cx, 12.5, cz);
        pressGrp.add(col);
        if (idx >= 2) {
          const spr = makeHelicalSpring(1.45, 9, 6, 0.24, MAT.brass, zone.id, 0);
          spr.position.set(cx, 2.5, cz);
          pressGrp.add(spr);
        }
      });
      const crown = cadMesh(new THREE.BoxGeometry(22, 3.2, 17.5), MAT.anodizedBlack, zone.id, 0, true);
      crown.position.y = 25;
      const cylBarrel = cadMesh(new THREE.CylinderGeometry(3.6, 3.6, 8, 20), MAT.signalOrange, zone.id, 0, true);
      cylBarrel.position.y = 30.5;
      const topFlange = makeBoltedFlange(4.6, 1.0, MAT.titanium, zone.id, 0);
      topFlange.position.y = 27.0;
      pressGrp.add(crown, cylBarrel, topFlange);
      const pressHead = new THREE.Group();
      const ramBlock = cadMesh(new THREE.BoxGeometry(16, 3.2, 13), MAT.brushedSteel, zone.id, 0, true);
      const pistonRod = new THREE.Mesh(new THREE.CylinderGeometry(1.5, 1.5, 13, 16), MAT.chrome);
      pistonRod.position.y = 7.5;
      const dieCart = makeDataCartridge(13, 2.2, 10, "ENVELOPE", "ver=1 oneof", "#0f172a", "#f97316", zone.id, 0);
      dieCart.position.y = -2.4;
      pressHead.add(ramBlock, pistonRod, dieCart);
      pressHead.position.y = 14;
      pressGrp.add(pressHead);
      refs.pressHead = pressHead;
      group.add(pressGrp);

      // Sub 1: 4-Byte Big-Endian Roller Conveyor & Laser Guillotine Gantry (dx: -10, dz: 18)
      const convGrp = new THREE.Group();
      convGrp.position.set(-10, 3.3, 18);
      convGrp.add(makeConveyorBed(30, 13, 12, zone.id, 1));
      refs.byteCubes = [];
      const byteLabels = [["B0:MSB", "0x00"], ["B1", "0x00"], ["B2", "0x00"], ["B3:LSB", "0x2A"]];
      byteLabels.forEach(([bTop, bHex], b) => {
        const cart = makeDataCartridge(4.5, 3.8, 5.2, bTop, bHex, "#1e293b", "#f97316", zone.id, 1);
        cart.position.set(-10.5 + b * 5.0, 5.0, 0);
        convGrp.add(cart);
        refs.byteCubes.push(cart);
      });
      const payloadCart = makeDataCartridge(7.8, 4.2, 7.2, "PAYLOAD", "42 BYTES", "#064e3b", "#10b981", zone.id, 1);
      payloadCart.position.set(10.5, 5.2, 0);
      convGrp.add(payloadCart);
      const archL = cadMesh(new THREE.BoxGeometry(2.2, 16, 2.2), MAT.titanium, zone.id, 1, true);
      archL.position.set(5.8, 8, 7.2);
      const archR = cadMesh(new THREE.BoxGeometry(2.2, 16, 2.2), MAT.titanium, zone.id, 1, true);
      archR.position.set(5.8, 8, -7.2);
      const archTop = cadMesh(new THREE.BoxGeometry(3.4, 2.8, 16.8), MAT.anodizedBlack, zone.id, 1, true);
      archTop.position.set(5.8, 16.5, 0);
      const emitterHs = makeHeatsink(3.0, 2.4, 10, 8, MAT.copper, zone.id, 1);
      emitterHs.position.set(5.8, 17.9, 0);
      const laserBlade = new THREE.Mesh(
        new THREE.PlaneGeometry(0.5, 13),
        new THREE.MeshBasicMaterial({ color: 0xf97316, side: THREE.DoubleSide })
      );
      laserBlade.rotation.y = Math.PI / 2;
      laserBlade.position.set(5.8, 8.5, 0);
      convGrp.add(archL, archR, archTop, emitterHs, laserBlade);
      group.add(convGrp);

      // Sub 2: FrameDecoder::feed() Caliper Gate + Staged Bellows + OLED Monitor (dx: 18, dz: 18)
      const decGrp = new THREE.Group();
      decGrp.position.set(18, 3.3, 18);
      decGrp.add(makeConveyorBed(22, 12, 8, zone.id, 2));
      const calPostL = cadMesh(new THREE.CylinderGeometry(1.1, 1.1, 14, 12), MAT.brushedSteel, zone.id, 2, true);
      calPostL.position.set(-6, 7, 6.2);
      const calPostR = cadMesh(new THREE.CylinderGeometry(1.1, 1.1, 14, 12), MAT.brushedSteel, zone.id, 2, true);
      calPostR.position.set(-6, 7, -6.2);
      const calBar = cadMesh(new THREE.BoxGeometry(2.6, 2.4, 14), MAT.signalOrange, zone.id, 2, true);
      calBar.position.set(-6, 13.5, 0);
      decGrp.add(calPostL, calPostR, calBar);

      const calScreen = makeDiagnosticScreen(15, 6.8, (ctx) => {
        ctx.fillStyle = "#f97316"; ctx.font = "bold 34px Consolas, monospace";
        ctx.fillText("1 MiB CALIPER GATE", 24, 54);
        ctx.fillStyle = "#10b981"; ctx.font = "26px Consolas, monospace";
        ctx.fillText("1 <= size <= 1,048,576 B", 24, 104);
        ctx.fillStyle = "#f87171"; ctx.fillText("0xFFFFFFFF -> FrameError!", 24, 154);
      }, zone.id, 2);
      calScreen.position.set(2, 15.5, -5.8);
      decGrp.add(calScreen);

      const tray = makePopulatedPCB(14, 9, zone.id, 2);
      tray.position.set(2, 3.4, 0.5);
      decGrp.add(tray);
      refs.decoderTray = tray;

      const attackCube = makeDataCartridge(11, 10, 11, "0xFFFFFFFF", "4 GiB BLOCKED!", "#450a0a", "#ef4444", zone.id, 2);
      attackCube.position.set(-6, 16, 0);
      attackCube.visible = false;
      decGrp.add(attackCube);
      refs.attackCube = attackCube;
      group.add(decGrp);

      // Sub 3: RAND_bytes(32) 3-Axis Precision Gimbal Gyroscope (dx: 34, dz: -18)
      const gyroPedestal = new THREE.Group();
      gyroPedestal.position.set(34, 3.3, -18);
      const pedBase = makeBoltedFlange(11, 3.2, MAT.darkIron, zone.id, 3);
      pedBase.position.y = 1.6;
      gyroPedestal.add(pedBase);
      const gyroGroup = new THREE.Group();
      gyroGroup.position.set(0, 15.5, 0);
      const ringMats = [MAT.brass, MAT.brushedSteel, MAT.signalOrange];
      for (let r = 0; r < 3; r++) {
        const ring = cadMesh(new THREE.TorusGeometry(9.5 - r * 2.2, 0.8, 14, 36), ringMats[r], zone.id, 3);
        gyroGroup.add(ring);
      }
      const coreSphere = cadMesh(new THREE.OctahedronGeometry(3.4, 1), MAT.amberGold, zone.id, 3, true);
      gyroGroup.add(coreSphere);
      gyroPedestal.add(gyroGroup);
      const dome = new THREE.Mesh(new THREE.CylinderGeometry(10.8, 10.8, 22, 24, 1, true), MAT.glass);
      dome.position.y = 14.5;
      gyroPedestal.add(dome);
      refs.gyroGroup = gyroGroup;
      group.add(gyroPedestal);
    }

    // ==================== ZONE 02: TLS 1.3 AIRLOCK & X.509 SAN SCANNER ====================
    else if (zone.id === "tls") {
      // Sub 0: 3 Octagonal Armored TLS 1.3 Bulkhead Rings + Longitudinal Tie-Beams (dx: -34, dz: -4)
      const tunnelGrp = new THREE.Group();
      tunnelGrp.position.set(-34, 3.3, -4);
      const cradle = cadMesh(new THREE.BoxGeometry(26, 3.2, 18), MAT.darkIron, zone.id, 0, true);
      cradle.position.y = 1.6;
      tunnelGrp.add(cradle);
      // 4 Longitudinal Structural Tie-Beams linking the 3 bulkheads
      for (let tb = 0; tb < 4; tb++) {
        const ang = (tb * Math.PI) / 2 + Math.PI / 4;
        const beam = new THREE.Mesh(new THREE.CylinderGeometry(0.65, 0.65, 22, 10), MAT.chrome);
        beam.rotation.z = Math.PI / 2;
        beam.position.set(0, 15 + Math.sin(ang) * 10.5, Math.cos(ang) * 10.5);
        tunnelGrp.add(beam);
      }
      refs.stargateRings = [];
      for (let r = 0; r < 3; r++) {
        const ringGrp = new THREE.Group();
        ringGrp.position.set(-9 + r * 9, 15, 0);
        const outerHousing = cadMesh(new THREE.TorusGeometry(11.5, 1.35, 8, 8), MAT.titanium, zone.id, 0, true);
        outerHousing.rotation.y = Math.PI / 2;
        const innerRotor = cadMesh(
          new THREE.TorusGeometry(9.0 - r * 0.5, 0.95, 12, 28),
          MAT.nominalEmerald.clone(),
          zone.id, 0
        );
        innerRotor.rotation.y = Math.PI / 2;
        ringGrp.add(outerHousing, innerRotor);
        tunnelGrp.add(ringGrp);
        refs.stargateRings.push(innerRotor);
      }
      group.add(tunnelGrp);

      // Sub 1: X.509 SAN Optical Laser Inspection Gantry & Bifurcated Truss Bridges (dx: -2, dz: 18)
      const sanGrp = new THREE.Group();
      sanGrp.position.set(-2, 3.3, 18);
      const x509Card = makePopulatedPCB(20, 11, zone.id, 1);
      x509Card.position.set(0, 1.5, 2);
      sanGrp.add(x509Card);
      const ipBridge = makeDataCartridge(21, 2.4, 4.8, "IP SAN: 127.0.0.1", "set1_ip_asc", "#1f2937", "#f59e0b", zone.id, 1);
      ipBridge.position.set(0, 11.2, 3.2);
      const dnsBridge = makeDataCartridge(21, 2.4, 4.8, "DNS SAN + SNI", "SSL_set1_host", "#1f2937", "#60a5fa", zone.id, 1);
      dnsBridge.position.set(0, 6.2, 3.2);
      // Moving Optical Laser Scanner Head sweeping across the bridges
      const scannerHead = new THREE.Group();
      const scanHousing = makeHeatsink(4.5, 2.5, 7, 5, MAT.copper, zone.id, 1);
      const scanBeam = new THREE.Mesh(
        new THREE.PlaneGeometry(0.4, 11),
        new THREE.MeshBasicMaterial({ color: 0x10b981, side: THREE.DoubleSide })
      );
      scanBeam.rotation.y = Math.PI / 2;
      scanBeam.position.y = -5.5;
      scannerHead.add(scanHousing, scanBeam);
      scannerHead.position.set(0, 14, 3.2);
      sanGrp.add(scannerHead);
      refs.sanScannerHead = scannerHead;

      const certScreen = makeDiagnosticScreen(20, 8, (ctx) => {
        ctx.fillStyle = "#10b981"; ctx.font = "bold 32px Consolas, monospace";
        ctx.fillText("X.509 PEER SAN SCANNER", 20, 48);
        ctx.fillStyle = "#f3f4f6"; ctx.font = "24px Consolas, monospace";
        ctx.fillText("IP Track : X509_VERIFY_PARAM_set1_ip_asc", 20, 95);
        ctx.fillStyle = "#93c5fd";
        ctx.fillText("DNS Track: SSL_set1_host + SNI Ext", 20, 138);
        ctx.fillStyle = "#fbbf24";
        ctx.fillText("Flag     : NEVER_CHECK_SUBJECT (No CN)", 20, 180);
      }, zone.id, 1);
      certScreen.position.set(0, 21.5, -5.5);
      sanGrp.add(ipBridge, dnsBridge, certScreen);
      group.add(sanGrp);

      // Sub 2: Server Cert + Private Key Meshing Twin Gearbox with Bearing Blocks (dx: 10, dz: -20)
      const keyBoxGrp = new THREE.Group();
      keyBoxGrp.position.set(10, 3.3, -20);
      keyBoxGrp.add(cadMesh(new THREE.BoxGeometry(22, 3.2, 13), MAT.darkIron, zone.id, 2, true));
      const pillarL = cadMesh(new THREE.BoxGeometry(2.5, 14, 8), MAT.titanium, zone.id, 2, true);
      pillarL.position.set(-9, 8, 0);
      const pillarR = cadMesh(new THREE.BoxGeometry(2.5, 14, 8), MAT.titanium, zone.id, 2, true);
      pillarR.position.set(9, 8, 0);
      const gearCert = makeGear(5.8, 4.3, 2.6, 12, MAT.brass, zone.id, 2);
      gearCert.position.set(-4.7, 10.5, 0);
      const gearKey = makeGear(5.8, 4.3, 2.6, 12, MAT.brushedSteel, zone.id, 2);
      gearKey.position.set(4.7, 10.5, 0);
      const glassCover = new THREE.Mesh(new THREE.BoxGeometry(16, 13, 6.5), MAT.glass);
      glassCover.position.set(0, 10, 0);
      keyBoxGrp.add(pillarL, pillarR, gearCert, gearKey, glassCover);
      refs.gearCert = gearCert;
      refs.gearKey = gearKey;
      group.add(keyBoxGrp);

      // Sub 3: SocketStream Railroad Turntable Switch + Outgoing Tracks (dx: 34, dz: 12)
      const swGrp = new THREE.Group();
      swGrp.position.set(34, 3.3, 12);
      const pit = makeBoltedFlange(11.5, 2.4, MAT.darkIron, zone.id, 3);
      pit.position.y = 1.2;
      swGrp.add(pit);
      const turntable = new THREE.Group();
      turntable.position.y = 3.2;
      const bridgeBeam = cadMesh(new THREE.BoxGeometry(20, 2.0, 6.2), MAT.signalOrange, zone.id, 3, true);
      const rail1 = cadMesh(new THREE.BoxGeometry(20.5, 0.9, 0.9), MAT.chrome, zone.id, 3);
      rail1.position.set(0, 1.3, 2.0);
      const rail2 = cadMesh(new THREE.BoxGeometry(20.5, 0.9, 0.9), MAT.chrome, zone.id, 3);
      rail2.position.set(0, 1.3, -2.0);
      const archHouse = cadMesh(new THREE.BoxGeometry(6, 7, 6.6), MAT.anodizedBlack, zone.id, 3, true);
      archHouse.position.y = 4.2;
      turntable.add(bridgeBeam, rail1, rail2, archHouse);
      swGrp.add(turntable);
      refs.switchCyl = turntable;
      group.add(swGrp);
    }

    // ==================== ZONE 03: PINGSERVER 64-BLADE RACK & STRAND MONORAIL ====================
    else if (zone.id === "server") {
      // Sub 0: 64-Blade Server Rack Cabinet + Side Axial Cooling Fans + Heatsink Top (dx: -34, dz: 12)
      const rackGrp = new THREE.Group();
      rackGrp.position.set(-34, 3.3, 12);
      const cabinet = cadMesh(new THREE.BoxGeometry(18, 34, 16), MAT.anodizedBlack, zone.id, 0, true);
      cabinet.position.y = 17;
      rackGrp.add(cabinet);
      const topSink = makeHeatsink(16, 3.2, 14, 10, MAT.titanium, zone.id, 0);
      topSink.position.y = 34.1;
      rackGrp.add(topSink);
      for (let f = 0; f < 3; f++) {
        const fan = makeCoolingFan(3.8, 1.6, zone.id, 0);
        fan.rotation.y = Math.PI / 2;
        fan.position.set(9.2, 8 + f * 9.5, 0);
        rackGrp.add(fan);
      }
      const bladeCanvas = document.createElement("canvas");
      bladeCanvas.width = 512; bladeCanvas.height = 768;
      const bCtx = bladeCanvas.getContext("2d");
      bCtx.fillStyle = "#0b0d11"; bCtx.fillRect(0, 0, 512, 768);
      bCtx.fillStyle = "#f59e0b"; bCtx.font = "bold 30px Consolas, monospace";
      bCtx.fillText("64-BLADE SESSION RACK", 36, 48);
      bCtx.fillStyle = "#10b981"; bCtx.font = "22px Consolas, monospace";
      bCtx.fillText("kMaxActiveSessions = 64", 36, 80);
      for (let r = 0; r < 16; r++) {
        for (let c = 0; c < 4; c++) {
          const bx = 28 + c * 116, by = 104 + r * 40;
          bCtx.fillStyle = "#1e2430"; bCtx.fillRect(bx, by, 104, 30);
          bCtx.fillStyle = "#10b981"; bCtx.fillRect(bx + 8, by + 9, 14, 12);
          bCtx.fillStyle = "#9ca3af"; bCtx.font = "15px Consolas, monospace";
          bCtx.fillText(`S${String(r * 4 + c + 1).padStart(2, "0")}`, bx + 32, by + 20);
        }
      }
      const rackFace = new THREE.Mesh(
        new THREE.PlaneGeometry(16.2, 31.5),
        new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(bladeCanvas) })
      );
      rackFace.position.set(0, 17, 8.1);
      rackGrp.add(rackFace);
      refs.rack = cabinet;
      group.add(rackGrp);

      // Sub 1: Single-File Strand Precision Monorail & 5 Articulated Work-Sleds (dx: 0, dz: 4)
      const monoGrp = new THREE.Group();
      monoGrp.position.set(0, 3.3, 4);
      const iBeam = cadMesh(new THREE.BoxGeometry(50, 3.2, 8.0), MAT.darkIron, zone.id, 1, true);
      iBeam.position.y = 4.5;
      const topRail = cadMesh(new THREE.BoxGeometry(50, 1.0, 2.6), MAT.amberGold, zone.id, 1, true);
      topRail.position.y = 6.5;
      const gateL = cadMesh(new THREE.BoxGeometry(6, 13, 2.2), MAT.titanium, zone.id, 1, true);
      gateL.position.set(0, 10, 5.2);
      const gateR = cadMesh(new THREE.BoxGeometry(6, 13, 2.2), MAT.titanium, zone.id, 1, true);
      gateR.position.set(0, 10, -5.2);
      const gateBridge = makeDataCartridge(11, 3.8, 12.5, "STRAND GATE", "1-AT-A-TIME", "#18181b", "#f59e0b", zone.id, 1);
      gateBridge.position.set(0, 16.5, 0);
      monoGrp.add(iBeam, topRail, gateL, gateR, gateBridge);

      refs.strandOrbs = [];
      for (let i = 0; i < 5; i++) {
        const sled = makeDataCartridge(5.4, 3.0, 5.4, `HND #${i + 1}`, "SERIALIZED", "#431407", "#f97316", zone.id, 1);
        sled.position.set(-20 + i * 10, 8.8, 0);
        monoGrp.add(sled);
        refs.strandOrbs.push(sled);
      }
      group.add(monoGrp);

      // Sub 2: weak_ptr<Impl> Cantilever Crane & Coiled Helical Spring Tether (dx: 14, dz: -20)
      const craneGrp = new THREE.Group();
      craneGrp.position.set(14, 3.3, -20);
      const mast = cadMesh(new THREE.BoxGeometry(4.2, 25, 4.2), MAT.titanium, zone.id, 2, true);
      mast.position.y = 12.5;
      const jib = cadMesh(new THREE.BoxGeometry(19, 2.6, 3.4), MAT.brushedSteel, zone.id, 2, true);
      jib.position.set(-5.5, 24, 0);
      const coilSpring = makeHelicalSpring(2.6, 9, 7, 0.38, MAT.copper, zone.id, 2);
      coilSpring.position.set(-11, 14.5, 0);
      const weakRing = cadMesh(new THREE.TorusGeometry(4.6, 0.85, 12, 28), MAT.nominalEmerald, zone.id, 2, true);
      weakRing.position.set(-11, 11.5, 0);
      craneGrp.add(mast, jib, coilSpring, weakRing);
      refs.weakRing = weakRing;
      group.add(craneGrp);

      // Sub 3: Deferred asio::post Return-Loop Chute + Valve Flanges (dx: 34, dz: 16)
      const postGrp = new THREE.Group();
      postGrp.position.set(34, 3.3, 16);
      const loopCurve = new THREE.CatmullRomCurve3([
        new THREE.Vector3(-9, 13, -6),
        new THREE.Vector3(7, 17, 0),
        new THREE.Vector3(7, 6.5, 8),
        new THREE.Vector3(-9, 6.5, 6)
      ]);
      const loopTube = cadMesh(new THREE.TubeGeometry(loopCurve, 28, 1.85, 12, false), MAT.amberGold, zone.id, 3);
      const postScreen = makeDiagnosticScreen(15, 6, (ctx) => {
        ctx.fillStyle = "#f59e0b"; ctx.font = "bold 34px Consolas, monospace";
        ctx.fillText("DEFERRED asio::post", 20, 56);
        ctx.fillStyle = "#f3f4f6"; ctx.font = "24px Consolas, monospace";
        ctx.fillText("Queues hub_.remove() at tail", 20, 108);
        ctx.fillStyle = "#10b981";
        ctx.fillText("Prevents broadcast iterator UB", 20, 152);
      }, zone.id, 3);
      postScreen.position.set(0, 21.5, -3);
      postGrp.add(loopTube, postScreen);
      group.add(postGrp);
    }

    // ==================== ZONE 04: CLIENTSESSION BUFFER TANK & IDLE CHRONOMETER ====================
    else if (zone.id === "session") {
      // Sub 0: Graduated Borosilicate Sight-Glass Buffer Tank + Level Gauge (dx: -34, dz: -14)
      const tankGrp = new THREE.Group();
      tankGrp.position.set(-34, 3.3, -14);
      const botFlange = makeBoltedFlange(11.2, 2.5, MAT.darkIron, zone.id, 0);
      botFlange.position.y = 1.25;
      const topFlange = makeBoltedFlange(11.2, 2.5, MAT.darkIron, zone.id, 0);
      topFlange.position.y = 27.5;
      const glassCyl = new THREE.Mesh(new THREE.CylinderGeometry(9.5, 9.5, 24, 24), MAT.glass.clone());
      glassCyl.position.y = 14.5;
      glassCyl.userData = { zoneId: zone.id, subIdx: 0 };
      clickableMeshes.push(glassCyl);
      tankGrp.add(botFlange, topFlange, glassCyl);
      for (let r = 0; r < 4; r++) {
        const ang = (r * Math.PI) / 2 + Math.PI / 4;
        const rod = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.6, 26, 10), MAT.chrome);
        rod.position.set(Math.cos(ang) * 10.2, 14.5, Math.sin(ang) * 10.2);
        tankGrp.add(rod);
      }
      refs.tankDiscs = [];
      for (let d = 0; d < 6; d++) {
        const disc = cadMesh(new THREE.CylinderGeometry(7.6, 7.6, 2.1, 20), MAT.laserBlue.clone(), zone.id, 0);
        disc.position.y = 4.2 + d * 3.2;
        tankGrp.add(disc);
        refs.tankDiscs.push(disc);
      }
      refs.tank = glassCyl;
      group.add(tankGrp);

      // Sub 1: Single async_write Crankshaft & Reciprocating Piston Pump (dx: -6, dz: 18)
      const pumpGrp = new THREE.Group();
      pumpGrp.position.set(-6, 3.3, 18);
      const pumpBody = cadMesh(new THREE.BoxGeometry(23, 7.5, 11.5), MAT.darkIron, zone.id, 1, true);
      pumpBody.position.y = 3.75;
      const nozzle = cadMesh(new THREE.CylinderGeometry(3.4, 3.4, 8, 16), MAT.brushedSteel, zone.id, 1, true);
      nozzle.rotation.z = Math.PI / 2;
      nozzle.position.set(14.5, 9.5, 0);
      const outFlange = makeBoltedFlange(4.6, 1.2, MAT.titanium, zone.id, 1);
      outFlange.rotation.z = Math.PI / 2;
      outFlange.position.set(18.2, 9.5, 0);
      const flyGear = makeGear(5.2, 3.8, 1.8, 14, MAT.brass, zone.id, 1);
      flyGear.position.set(-8, 11, 6.4);
      const piston = makeDataCartridge(11, 5.2, 8.2, "async_write", "1 WRITER ONLY", "#064e3b", "#10b981", zone.id, 1);
      piston.position.set(-1, 10.5, 0);
      pumpGrp.add(pumpBody, nozzle, outFlange, flyGear, piston);
      refs.piston = piston;
      refs.pumpGear = flyGear;
      group.add(pumpGrp);

      // Sub 2: Half-Close Dual-Gate Airlock Valve Chamber (dx: 18, dz: -16)
      const eofGrp = new THREE.Group();
      eofGrp.position.set(18, 3.3, -16);
      const chamber = cadMesh(new THREE.CylinderGeometry(7.5, 7.5, 18, 20), MAT.titanium, zone.id, 2, true);
      chamber.rotation.z = Math.PI / 2;
      chamber.position.y = 8;
      const flL = makeBoltedFlange(8.8, 1.5, MAT.darkIron, zone.id, 2);
      flL.rotation.z = Math.PI / 2; flL.position.set(-9, 8, 0);
      const flR = makeBoltedFlange(8.8, 1.5, MAT.darkIron, zone.id, 2);
      flR.rotation.z = Math.PI / 2; flR.position.set(9, 8, 0);
      const valveWheel = cadMesh(new THREE.TorusGeometry(4.2, 0.55, 10, 20), MAT.signalOrange, zone.id, 2);
      valveWheel.rotation.x = Math.PI / 2;
      valveWheel.position.y = 17.5;
      eofGrp.add(chamber, flL, flR, valveWheel);
      group.add(eofGrp);

      // Sub 3: 30s Lazy Idle Gear Chronometer Tower (dx: 34, dz: 14)
      const clockGrp = new THREE.Group();
      clockGrp.position.set(34, 3.3, 14);
      const tower = cadMesh(new THREE.BoxGeometry(14, 28, 10), MAT.anodizedBlack, zone.id, 3, true);
      tower.position.y = 14;
      const escapementGear = makeGear(4.5, 3.2, 1.6, 12, MAT.brass, zone.id, 3);
      escapementGear.position.set(-2.5, 9.5, 5.6);
      const secondGear = makeGear(3.2, 2.2, 1.6, 10, MAT.brushedSteel, zone.id, 3);
      secondGear.position.set(3.5, 7.5, 5.6);
      clockGrp.add(tower, escapementGear, secondGear);
      refs.clockGear = escapementGear;
      refs.clockGear2 = secondGear;

      const dialC = document.createElement("canvas");
      dialC.width = 256; dialC.height = 256;
      const dCtx = dialC.getContext("2d");
      dCtx.fillStyle = "#0d1017"; dCtx.beginPath(); dCtx.arc(128, 128, 118, 0, Math.PI * 2); dCtx.fill();
      dCtx.strokeStyle = "#f97316"; dCtx.lineWidth = 8; dCtx.stroke();
      for (let m = 0; m < 12; m++) {
        const a = (m * Math.PI) / 6;
        dCtx.strokeStyle = "#e5e7eb"; dCtx.lineWidth = 4;
        dCtx.beginPath();
        dCtx.moveTo(128 + Math.cos(a) * 90, 128 + Math.sin(a) * 90);
        dCtx.lineTo(128 + Math.cos(a) * 108, 128 + Math.sin(a) * 108);
        dCtx.stroke();
      }
      dCtx.fillStyle = "#f97316"; dCtx.font = "bold 26px Consolas, monospace"; dCtx.textAlign = "center";
      dCtx.fillText("30s IDLE", 128, 92);
      const dialMesh = new THREE.Mesh(
        new THREE.CircleGeometry(6.2, 28),
        new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(dialC) })
      );
      dialMesh.position.set(0, 21, 5.2);
      const clockHand = cadMesh(new THREE.BoxGeometry(1.1, 5.2, 0.8), MAT.signalOrange, zone.id, 3);
      clockHand.position.set(0, 21, 5.6);
      clockGrp.add(dialMesh, clockHand);
      refs.clockHand = clockHand;
      group.add(clockGrp);
    }

    // ==================== ZONE 05: CHATHUB SWITCHYARD & R7 PRIVACY BLAST SHIELD ====================
    else if (zone.id === "hub") {
      // Sub 0: Anti-Spoofing Laser Scanner Gate & 3-Arm Turnstile (dx: -34, dz: 14)
      const gateGrp = new THREE.Group();
      gateGrp.position.set(-34, 3.3, 14);
      const pilL = cadMesh(new THREE.BoxGeometry(3.2, 22, 3.2), MAT.titanium, zone.id, 0, true);
      pilL.position.set(-7.5, 11, 0);
      const pilR = cadMesh(new THREE.BoxGeometry(3.2, 22, 3.2), MAT.titanium, zone.id, 0, true);
      pilR.position.set(7.5, 11, 0);
      const gateCrown = makeDataCartridge(19, 4.2, 5.5, "ANTI-SPOOF", "sender == \"\"", "#450a0a", "#ef4444", zone.id, 0);
      gateCrown.position.set(0, 23, 0);
      const plasmaArch = cadMesh(new THREE.TorusGeometry(6.4, 0.75, 12, 28), MAT.hazardRed, zone.id, 0);
      plasmaArch.position.set(0, 11, 0);
      gateGrp.add(pilL, pilR, gateCrown, plasmaArch);
      refs.plasmaArch = plasmaArch;
      group.add(gateGrp);

      // Sub 1: Central usernames_ Commutator Drum & R4 Radar Dish (dx: -12, dz: -18)
      const regGrp = new THREE.Group();
      regGrp.position.set(-12, 3.3, -18);
      const drum = cadMesh(new THREE.CylinderGeometry(8.5, 9.5, 20, 20), MAT.darkIron, zone.id, 1, true);
      drum.position.y = 10;
      for (let r = 0; r < 3; r++) {
        const slipRing = new THREE.Mesh(new THREE.TorusGeometry(9.2, 0.55, 8, 24), MAT.copper);
        slipRing.rotation.x = Math.PI / 2;
        slipRing.position.y = 5 + r * 5;
        regGrp.add(slipRing);
      }
      const dish = cadMesh(new THREE.ConeGeometry(8.0, 5.5, 18, 1, true), MAT.brushedSteel, zone.id, 1, true);
      dish.rotation.x = Math.PI;
      dish.position.y = 24;
      regGrp.add(drum, dish);
      refs.radarDish = dish;
      group.add(regGrp);

      // Sub 2: 3-Lane Private DM Switchyard (Alice, Bob, Charlie Mini-Workstations + R7 Guillotine) (dx: 14, dz: 18)
      const dmGrp = new THREE.Group();
      dmGrp.position.set(14, 3.3, 18);
      [
        { name: "ALICE", z: -7.5, col: "#10b981", sub: "DM PEER" },
        { name: "BOB", z: 0, col: "#10b981", sub: "DM PEER" },
        { name: "CHARLIE", z: 7.5, col: "#ef4444", sub: "0 BYTES!" }
      ].forEach(peer => {
        const track = new THREE.Mesh(new THREE.BoxGeometry(18, 0.8, 2.4), MAT.titanium);
        track.position.set(0, 0.5, peer.z);
        const booth = makeDataCartridge(7.5, 7.2, 5.0, peer.name, peer.sub, "#111827", peer.col, zone.id, 2);
        booth.position.set(9.5, 4.2, peer.z);
        dmGrp.add(track, booth);
      });
      const shieldFrame = cadMesh(new THREE.BoxGeometry(3.8, 18, 7.0), MAT.darkIron, zone.id, 2, true);
      shieldFrame.position.set(0, 9, 7.5);
      const blastDoor = makeDataCartridge(2.6, 12, 5.8, "R7 SHIELD", "0B LEAKED", "#7f1d1d", "#ef4444", zone.id, 2);
      blastDoor.position.set(0, 10, 7.5);
      dmGrp.add(shieldFrame, blastDoor);
      refs.blastDoor = blastDoor;
      group.add(dmGrp);

      // Sub 3: Voluntary Group Ring-Bus Rotunda (R8-R11) (dx: 34, dz: -14)
      const grpRot = new THREE.Group();
      grpRot.position.set(34, 3.3, -14);
      const ringBus = cadMesh(new THREE.TorusGeometry(9.5, 1.4, 12, 24), MAT.nominalEmerald, zone.id, 3, true);
      ringBus.rotation.x = Math.PI / 2;
      ringBus.position.y = 8;
      grpRot.add(ringBus);
      for (let m = 0; m < 4; m++) {
        const a = (m * Math.PI) / 2;
        const pod = cadMesh(new THREE.BoxGeometry(4.8, 9.5, 4.8), m < 3 ? MAT.brushedSteel : MAT.darkIron, zone.id, 3, true);
        pod.position.set(Math.cos(a) * 9.5, 4.8, Math.sin(a) * 9.5);
        grpRot.add(pod);
      }
      group.add(grpRot);
    }

    // ==================== ZONE 06: SQLITE WAL BANK VAULT & MAGNETIC SPINDLE ====================
    else if (zone.id === "sqlite") {
      // Sub 0: BEGIN IMMEDIATE 8-Bolt Circular Steel Bank Vault Door (dx: -34, dz: 14)
      const vaultGrp = new THREE.Group();
      vaultGrp.position.set(-34, 3.3, 14);
      const wallFrame = cadMesh(new THREE.BoxGeometry(6.5, 26, 26), MAT.darkIron, zone.id, 0, true);
      wallFrame.position.y = 13;
      vaultGrp.add(wallFrame);

      const doorPivot = new THREE.Group();
      doorPivot.position.set(3.4, 13, 0);
      const steelDoor = cadMesh(new THREE.CylinderGeometry(10.6, 10.6, 3.0, 28), MAT.brushedSteel, zone.id, 0, true);
      steelDoor.rotation.z = Math.PI / 2;
      doorPivot.add(steelDoor);
      for (let b = 0; b < 8; b++) {
        const ang = (b * Math.PI) / 4;
        const bolt = new THREE.Mesh(new THREE.CylinderGeometry(0.8, 0.8, 4.8, 10), MAT.chrome);
        bolt.position.set(1.3, Math.cos(ang) * 8.6, Math.sin(ang) * 8.6);
        bolt.rotation.x = ang;
        doorPivot.add(bolt);
      }
      const wheelGear = makeGear(5.0, 3.6, 1.2, 8, MAT.signalOrange, zone.id, 0);
      wheelGear.rotation.y = Math.PI / 2;
      wheelGear.position.x = 2.4;
      doorPivot.add(wheelGear);
      vaultGrp.add(doorPivot);
      refs.vaultDoor = doorPivot;
      group.add(vaultGrp);

      // Sub 1: EVP_sha256 Twin-Gear Crystal Hash Crusher Mill (dx: -8, dz: -18)
      const shaGrp = new THREE.Group();
      shaGrp.position.set(-8, 3.3, -18);
      const hopper = cadMesh(new THREE.BoxGeometry(17, 6.5, 12.5), MAT.darkIron, zone.id, 1, true);
      hopper.position.y = 3.25;
      const millGear1 = makeGear(4.6, 3.2, 3.5, 10, MAT.titanium, zone.id, 1);
      millGear1.position.set(-3.8, 8.5, 0);
      const millGear2 = makeGear(4.6, 3.2, 3.5, 10, MAT.titanium, zone.id, 1);
      millGear2.position.set(3.8, 8.5, 0);
      const shaCrystal = cadMesh(new THREE.OctahedronGeometry(6.8, 1), MAT.laserBlue, zone.id, 1, true);
      shaCrystal.position.y = 17.5;
      shaGrp.add(hopper, millGear1, millGear2, shaCrystal);
      refs.shaCrystal = shaCrystal;
      refs.millGear1 = millGear1;
      refs.millGear2 = millGear2;
      group.add(shaGrp);

      // Sub 2: Schema v1 -> v2 Scissor Pantograph Lift & SQL Table PCB (dx: 14, dz: 18)
      const liftGrp = new THREE.Group();
      liftGrp.position.set(14, 3.3, 18);
      liftGrp.add(makeScissorLift(16, 11, 10, zone.id, 2));
      const v2Screen = makeDiagnosticScreen(16, 6.5, (ctx) => {
        ctx.fillStyle = "#f97316"; ctx.font = "bold 34px Consolas, monospace";
        ctx.fillText("SCHEMA V1 -> V2 MIGRATION", 20, 52);
        ctx.fillStyle = "#10b981"; ctx.font = "24px Consolas, monospace";
        ctx.fillText("row_number() OVER (PARTITION BY room)", 20, 102);
        ctx.fillStyle = "#93c5fd";
        ctx.fillText("nullif(?5, '') -> NULL for empty key", 20, 150);
      }, zone.id, 2);
      v2Screen.position.set(0, 16, 0);
      liftGrp.add(v2Screen);
      group.add(liftGrp);

      // Sub 3: 3-Platter SQLite WAL Magnetic Spindle & Voice-Coil Actuators (dx: 34, dz: -12)
      const hddGrp = new THREE.Group();
      hddGrp.position.set(34, 3.3, -12);
      const hddBase = cadMesh(new THREE.BoxGeometry(24, 2.4, 24), MAT.darkIron, zone.id, 3, true);
      hddBase.position.y = 1.2;
      const spindleHub = new THREE.Mesh(new THREE.CylinderGeometry(2.4, 2.4, 24, 16), MAT.chrome);
      spindleHub.position.y = 12;
      hddGrp.add(hddBase, spindleHub);
      refs.walPlatters = [];
      refs.hddArms = [];
      for (let p = 0; p < 3; p++) {
        const platterGrp = new THREE.Group();
        platterGrp.position.y = 5.5 + p * 6.5;
        const disc = cadMesh(new THREE.CylinderGeometry(11.2, 11.2, 1.3, 32), MAT.brushedSteel, zone.id, 3, true);
        const trackRing = new THREE.Mesh(new THREE.RingGeometry(4.8, 9.4, 28), MAT.signalOrange);
        trackRing.rotation.x = -Math.PI / 2;
        trackRing.position.y = 0.72;
        platterGrp.add(disc, trackRing);
        hddGrp.add(platterGrp);
        refs.walPlatters.push(platterGrp);

        const arm = cadMesh(new THREE.BoxGeometry(9.5, 0.9, 1.8), MAT.anodizedBlack, zone.id, 3, true);
        arm.position.set(-7, 6.8 + p * 6.5, 5.2);
        arm.rotation.y = -0.35;
        hddGrp.add(arm);
        refs.hddArms.push(arm);
      }
      group.add(hddGrp);
    }

    // ==================== ZONE 07: PINGCLIENT RAILGUN & OPTICAL DEMUX PRISM ====================
    else if (zone.id === "client") {
      // Sub 0: 3-Stage Single-Deadline Connect Gantry (3.0s) (dx: -34, dz: -16)
      const gantryGrp = new THREE.Group();
      gantryGrp.position.set(-34, 3.3, -16);
      gantryGrp.add(makeConveyorBed(22, 11, 8, zone.id, 0));
      ["DNS", "TCP", "TLS"].forEach((lbl, idx) => {
        const arch = makeDataCartridge(5.2, 14, 11.5, lbl, "3.0s MAX", "#1e293b", "#60a5fa", zone.id, 0);
        arch.position.set(-6.8 + idx * 6.8, 8.5, 0);
        gantryGrp.add(arch);
      });
      group.add(gantryGrp);

      // Sub 1: Phase 1 context_.run_one() 6-Coil Electromagnetic Railgun + Projectile (dx: -8, dz: 18)
      const railGrp = new THREE.Group();
      railGrp.position.set(-8, 3.3, 18);
      const barrel = cadMesh(new THREE.BoxGeometry(25, 4.5, 7.5), MAT.darkIron, zone.id, 1, true);
      barrel.position.y = 6.5;
      const railSink = makeHeatsink(22, 2.5, 6.5, 10, MAT.titanium, zone.id, 1);
      railSink.position.y = 1.25;
      railGrp.add(barrel, railSink);
      for (let c = 0; c < 6; c++) {
        const coil = cadMesh(new THREE.TorusGeometry(5.0, 0.85, 10, 22), MAT.copper, zone.id, 1);
        coil.rotation.y = Math.PI / 2;
        coil.position.set(-9.5 + c * 3.8, 6.8, 0);
        railGrp.add(coil);
      }
      const railSled = cadMesh(new THREE.BoxGeometry(4.5, 2.5, 3.5), MAT.signalOrange, zone.id, 1, true);
      railSled.position.set(0, 9.8, 0);
      railGrp.add(railSled);
      refs.railSled = railSled;
      group.add(railGrp);

      // Sub 2: Phase 2 Optical Beam-Splitter Prism & events_ Hopper Magazine (dx: 18, dz: 14)
      const prismGrp = new THREE.Group();
      prismGrp.position.set(18, 3.3, 14);
      const table = makeBoltedFlange(9.5, 3.0, MAT.brass, zone.id, 2);
      table.position.y = 1.5;
      const prism = cadMesh(new THREE.CylinderGeometry(7.8, 7.8, 12.5, 3), MAT.laserBlue, zone.id, 2, true);
      prism.position.y = 10.2;
      const hopperMag = makeDataCartridge(11, 6.5, 6.5, "events_", "DEQUE HOPPER", "#1e293b", "#f59e0b", zone.id, 2);
      hopperMag.position.set(0, 4.2, -10.5);
      // Visible Straight Ack Beam & 90-Degree Refracted Event Beam!
      const ackBeam = new THREE.Mesh(new THREE.CylinderGeometry(0.45, 0.45, 12, 8), MAT.nominalEmerald);
      ackBeam.rotation.z = Math.PI / 2;
      ackBeam.position.set(7.5, 9.5, 0);
      const evtBeam = new THREE.Mesh(new THREE.CylinderGeometry(0.45, 0.45, 10, 8), MAT.amberGold);
      evtBeam.rotation.x = Math.PI / 2;
      evtBeam.position.set(0, 8.5, -6.5);
      prismGrp.add(table, prism, hopperMag, ackBeam, evtBeam);
      refs.prism = prism;
      group.add(prismGrp);

      // Sub 3: Soft socket().cancel() Disc Caliper Brake (dx: 34, dz: -16)
      const brakeGrp = new THREE.Group();
      brakeGrp.position.set(34, 3.3, -16);
      const rotorDisc = makeGear(8.8, 7.8, 1.8, 24, MAT.brushedSteel, zone.id, 3);
      rotorDisc.position.y = 10.5;
      const caliper = makeDataCartridge(9.5, 5.5, 4.8, "cancel()", "SOFT POLL", "#064e3b", "#10b981", zone.id, 3);
      caliper.position.set(0, 16.5, 0);
      brakeGrp.add(rotorDisc, caliper);
      refs.brakeDisc = rotorDisc;
      group.add(brakeGrp);
    }

    // ==================== ZONE 08: CHATWORKER DRAWBRIDGE & RECOVERY QUADCOPTER ====================
    else if (zone.id === "worker") {
      // Sub 0: submit() Upfront 256-Bit Hydraulic Key Coining Press (dx: -34, dz: 14)
      const forgeGrp = new THREE.Group();
      forgeGrp.position.set(-34, 3.3, 14);
      const anvil = cadMesh(new THREE.BoxGeometry(16, 8, 13), MAT.darkIron, zone.id, 0, true);
      anvil.position.y = 4;
      const pressArch = cadMesh(new THREE.BoxGeometry(4, 18, 14), MAT.titanium, zone.id, 0, true);
      pressArch.position.y = 9;
      const keyWafer = makeDataCartridge(12, 4.8, 9.5, "256-BIT KEY", "MINTED UPFRONT", "#451a03", "#f59e0b", zone.id, 0);
      keyWafer.position.y = 11.5;
      forgeGrp.add(anvil, pressArch, keyWafer);
      group.add(forgeGrp);

      // Sub 1: cv_.wait_for(stop_token) Helical Stasis Chamber (dx: -8, dz: -18)
      const podGrp = new THREE.Group();
      podGrp.position.set(-8, 3.3, -18);
      const podBase = makeBoltedFlange(8.2, 2.4, MAT.darkIron, zone.id, 1);
      podBase.position.y = 1.2;
      const podTop = makeBoltedFlange(8.2, 2.4, MAT.darkIron, zone.id, 1);
      podTop.position.y = 20.5;
      const podHelix = makeHelicalSpring(4.8, 17, 6, 0.55, MAT.violetLogic, zone.id, 1);
      podHelix.position.y = 2.2;
      const podGlass = new THREE.Mesh(new THREE.CylinderGeometry(6.8, 6.8, 18, 20), MAT.glass);
      podGlass.position.y = 11;
      podGrp.add(podBase, podTop, podHelix, podGlass);
      group.add(podGrp);

      // Sub 2: Bascule Suspension Drawbridge & 4-Rotor Autonomous Recovery Quadcopter (dx: 14, dz: 12)
      const bridgeGrp = new THREE.Group();
      bridgeGrp.position.set(14, 3.3, 12);
      const towerL = cadMesh(new THREE.BoxGeometry(3.5, 18, 11), MAT.darkIron, zone.id, 2, true);
      towerL.position.set(-15, 9, 0);
      const towerR = cadMesh(new THREE.BoxGeometry(3.5, 18, 11), MAT.darkIron, zone.id, 2, true);
      towerR.position.set(15, 9, 0);
      const bridgeDeck = cadMesh(new THREE.BoxGeometry(27, 2.4, 9.5), MAT.titanium.clone(), zone.id, 2, true);
      bridgeDeck.position.y = 8;
      bridgeGrp.add(towerL, towerR, bridgeDeck);
      refs.bridge = bridgeDeck;

      const drone = new THREE.Group();
      drone.position.set(0, 22, 0);
      const fuselage = cadMesh(new THREE.BoxGeometry(7.5, 2.8, 5.2), MAT.signalOrange, zone.id, 2, true);
      const cargoKey = makeDataCartridge(5.2, 2.4, 4.5, "KEY+TOKEN", "PRESERVED", "#451a03", "#f59e0b", zone.id, 2);
      cargoKey.position.y = -2.8;
      drone.add(fuselage, cargoKey);

      refs.droneRotors = [];
      [[-6.0, -4.8], [6.0, -4.8], [-6.0, 4.8], [6.0, 4.8]].forEach(([rx, rz]) => {
        const arm = new THREE.Mesh(new THREE.CylinderGeometry(0.45, 0.45, 8.2, 8), MAT.carbonFiber);
        arm.position.set(rx * 0.5, 0, rz * 0.5);
        arm.rotation.z = Math.PI / 2;
        arm.rotation.y = Math.atan2(rz, rx);
        const motor = new THREE.Mesh(new THREE.CylinderGeometry(1.2, 1.2, 2.0, 12), MAT.brushedSteel);
        motor.position.set(rx, 0.9, rz);
        const blade = new THREE.Mesh(new THREE.BoxGeometry(6.0, 0.25, 0.75), MAT.chrome);
        blade.position.set(rx, 2.0, rz);
        drone.add(arm, motor, blade);
        refs.droneRotors.push(blade);
      });
      bridgeGrp.add(drone);
      refs.drone = drone;
      group.add(bridgeGrp);

      // Sub 3: terminal_ Dual-Latch Signal Tower (dx: 34, dz: -14)
      const termGrp = new THREE.Group();
      termGrp.position.set(34, 3.3, -14);
      const towerBase = cadMesh(new THREE.BoxGeometry(12, 14, 10), MAT.anodizedBlack, zone.id, 3, true);
      towerBase.position.y = 7;
      const beacon = makeDataCartridge(12, 6, 9, "terminal_", "BEFORE running_", "#450a0a", "#ef4444", zone.id, 3);
      beacon.position.y = 17;
      termGrp.add(towerBase, beacon);
      group.add(termGrp);
    }

    // ==================== ZONE 09: ROOMMODEL AS/RS WAREHOUSE & BALANCE SCALE ====================
    else if (zone.id === "model") {
      // Sub 0: Robotic Clamp & Welder Cell (dx: -34, dz: -16)
      const clampGrp = new THREE.Group();
      clampGrp.position.set(-34, 3.3, -16);
      clampGrp.add(makeScissorLift(16, 11, 6, zone.id, 0));
      const clampBox = makeDataCartridge(15, 5.5, 9.5, "direct-view-1", "-> direct-1", "#1e1b4b", "#818cf8", zone.id, 0);
      clampBox.position.y = 11;
      clampGrp.add(clampBox);
      group.add(clampGrp);

      // Sub 1: Twin-Pan Analytical Balance Scale (Draft Comparator) (dx: -6, dz: 18)
      const scaleGrp = new THREE.Group();
      scaleGrp.position.set(-6, 3.3, 18);
      const scaleBase = makeBoltedFlange(7.5, 2.2, MAT.darkIron, zone.id, 1);
      scaleBase.position.y = 1.1;
      const pillar = cadMesh(new THREE.CylinderGeometry(1.4, 2.1, 18, 16), MAT.brass, zone.id, 1, true);
      pillar.position.y = 10;
      const beam = cadMesh(new THREE.BoxGeometry(21, 1.5, 2.4), MAT.brushedSteel, zone.id, 1, true);
      beam.position.y = 19;
      const rodL = new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.25, 6, 8), MAT.chrome);
      rodL.position.set(-8.8, 16, 0);
      const rodR = new THREE.Mesh(new THREE.CylinderGeometry(0.25, 0.25, 6, 8), MAT.chrome);
      rodR.position.set(8.8, 16, 0);
      const panL = makeDataCartridge(7.5, 3.0, 6.2, "DRAFT", "room.draft", "#0f172a", "#10b981", zone.id, 1);
      panL.position.set(-8.8, 12.5, 0);
      const panR = makeDataCartridge(7.5, 3.0, 6.2, "SENT", "submitted", "#0f172a", "#f97316", zone.id, 1);
      panR.position.set(8.8, 12.5, 0);
      scaleGrp.add(scaleBase, pillar, beam, rodL, rodR, panL, panR);
      refs.scaleBeam = beam;
      group.add(scaleGrp);

      // Sub 2: Mechanized Quarantine Airlock Bay (dx: 16, dz: -18)
      const qGrp = new THREE.Group();
      qGrp.position.set(16, 3.3, -18);
      const airlockFrame = cadMesh(new THREE.BoxGeometry(15, 14, 10), MAT.darkIron, zone.id, 2, true);
      airlockFrame.position.y = 7;
      const qGate = makeDataCartridge(13, 6, 9, "QUARANTINE", "available=false", "#451a03", "#f59e0b", zone.id, 2);
      qGate.position.y = 16;
      qGrp.add(airlockFrame, qGate);
      group.add(qGrp);

      // Sub 3: 3-Aisle AS/RS Storage Rack Matrix & Moving Overhead Crane (dx: 32, dz: 14)
      const rackMatGrp = new THREE.Group();
      rackMatGrp.position.set(32, 3.3, 14);
      const gantryFrame = cadMesh(new THREE.BoxGeometry(22, 1.6, 22), MAT.titanium, zone.id, 3, true);
      gantryFrame.position.y = 19;
      const craneCarriage = cadMesh(new THREE.BoxGeometry(22.5, 2.2, 3.5), MAT.signalOrange, zone.id, 3, true);
      craneCarriage.position.y = 20.6;
      rackMatGrp.add(gantryFrame, craneCarriage);
      refs.asrsCrane = craneCarriage;

      refs.roomCubes = [];
      for (let gx = -1; gx <= 1; gx++) {
        for (let gz = -1; gz <= 1; gz++) {
          const cell = cadMesh(new THREE.BoxGeometry(5.2, 8 + (gx + gz + 2) * 1.8, 5.2), MAT.violetLogic, zone.id, 3, true);
          cell.position.set(gx * 7.0, 6, gz * 7.0);
          rackMatGrp.add(cell);
          refs.roomCubes.push(cell);
        }
      }
      group.add(rackMatGrp);
    }

    // ==================== ZONE 10: CLI TERMINAL, WIN32 RIG & CMAKE PYRAMID ====================
    else {
      // Sub 0: Developer Cockpit Monitor + PC Tower w/ Cooling Fan + Keyboard (dx: -34, dz: -14)
      const cliGrp = new THREE.Group();
      cliGrp.position.set(-34, 3.3, -14);
      const stand = cadMesh(new THREE.BoxGeometry(4.5, 8, 4.5), MAT.titanium, zone.id, 0, true);
      stand.position.set(-2, 4, -1);
      const monScreen = makeDiagnosticScreen(18, 11, (ctx) => {
        ctx.fillStyle = "#10b981"; ctx.font = "bold 30px Consolas, monospace";
        ctx.fillText("WIN32 CONSOLE UTF-16->8", 20, 48);
        ctx.fillStyle = "#f3f4f6"; ctx.font = "24px Consolas, monospace";
        ctx.fillText("0xD83D + 0xDE00 Surrogates", 20, 95);
        ctx.fillStyle = "#f97316";
        ctx.fillText("-> [F0][9F][98][80] (4B)", 20, 140);
        ctx.fillStyle = "#9ca3af";
        ctx.fillText("PeekNamedPipe Non-Blocking", 20, 185);
      }, zone.id, 0);
      monScreen.position.set(-2, 13, -1);
      monScreen.rotation.x = -0.14;
      const pcTower = cadMesh(new THREE.BoxGeometry(5.5, 14, 10), MAT.anodizedBlack, zone.id, 0, true);
      pcTower.position.set(10, 7, 0);
      const pcFan = makeCoolingFan(2.2, 1.0, zone.id, 0);
      pcFan.position.set(10, 8, 5.2);
      const keyboard = makePopulatedPCB(14, 6.2, zone.id, 0);
      keyboard.position.set(-2, 0.8, 7);
      cliGrp.add(stand, monScreen, pcTower, pcFan, keyboard);
      group.add(cliGrp);

      // Sub 1: 4-Byte UTF-8 Train & Parabolic ANSI Escape Deflector Shield (dx: -8, dz: 18)
      const shieldGrp = new THREE.Group();
      shieldGrp.position.set(-8, 3.3, 18);
      shieldGrp.add(makeConveyorBed(18, 10, 6, zone.id, 1));
      const ansiShield = makeDataCartridge(15, 9, 4.5, "escape_text()", "BLOCKS \\x1b[2J", "#064e3b", "#10b981", zone.id, 1);
      ansiShield.position.y = 7.5;
      shieldGrp.add(ansiShield);
      group.add(shieldGrp);

      // Sub 2: Exploded 3D Win32 Multi-Pane Desktop Window Rig + 40ms Timer Gear (dx: 16, dz: -14)
      const guiGrp = new THREE.Group();
      guiGrp.position.set(16, 3.3, -14);
      const winFrame = makeDiagnosticScreen(20, 13.5, (ctx) => {
        ctx.fillStyle = "#60a5fa"; ctx.font = "bold 30px Consolas, monospace";
        ctx.fillText("WIN32 HWND WORKSTATION", 20, 46);
        ctx.strokeStyle = "#60a5fa"; ctx.strokeRect(20, 65, 140, 160);
        ctx.strokeStyle = "#10b981"; ctx.strokeRect(175, 65, 310, 115);
        ctx.strokeStyle = "#f97316"; ctx.strokeRect(175, 190, 310, 35);
        ctx.fillStyle = "#f3f4f6"; ctx.font = "22px Consolas, monospace";
        ctx.fillText("Rooms", 42, 105);
        ctx.fillText("Transcript (\\0 -> L'␀')", 190, 115);
        ctx.fillText("Draft + 40ms WM_TIMER", 190, 215);
      }, zone.id, 2);
      winFrame.position.y = 11.5;
      const timerGear = makeGear(4.2, 3.0, 1.5, 12, MAT.brass, zone.id, 2);
      timerGear.position.set(9.5, 6, 2.2);
      guiGrp.add(winFrame, timerGear);
      refs.guiGear = timerGear;
      group.add(guiGrp);

      // Sub 3: 6-Tier Stepped CMake Dependency Ziggurat with Copper Busbars (dx: 34, dz: 16)
      const pyrGrp = new THREE.Group();
      pyrGrp.position.set(34, 3.3, 16);
      const tiers = ["chat_proto", "chat_framing", "chat_transport", "server/client", "gui_model", "202 TESTS"];
      tiers.forEach((tName, tIdx) => {
        const w = 18.5 - tIdx * 2.2;
        const slab = cadMesh(
          new THREE.BoxGeometry(w, 2.8, w),
          tIdx === 5 ? MAT.signalOrange : (tIdx % 2 === 0 ? MAT.brushedSteel : MAT.titanium),
          zone.id, 3, true
        );
        slab.position.y = 1.4 + tIdx * 3.0;
        pyrGrp.add(slab);
      });
      refs.cmakePyr = pyrGrp;
      group.add(pyrGrp);
    }

    scene.add(group);
    machineGroups[zone.id] = group;
  });

  // ==================== ELEVATED INDUSTRIAL DATA CONDUITS & CARGO PODS ====================
  const highwayCurves = [];
  const packetDrones = [];

  HIGHWAY_LINKS.forEach(([idA, idB], idx) => {
    const za = ZONES_3D.find(z => z.id === idA);
    const zb = ZONES_3D.find(z => z.id === idB);
    if (!za || !zb) return;

    const midX = (za.x + zb.x) * 0.5;
    const midZ = (za.z + zb.z) * 0.5;
    const curve = new THREE.CatmullRomCurve3([
      new THREE.Vector3(za.x, 4.5, za.z),
      new THREE.Vector3(midX, 16, midZ),
      new THREE.Vector3(zb.x, 4.5, zb.z)
    ]);
    highwayCurves.push({ idA, idB, curve });

    const tubeGeo = new THREE.TubeGeometry(curve, 36, 0.75, 8, false);
    const tubeMat = new THREE.MeshStandardMaterial({
      color: 0x374151,
      metalness: 0.75,
      roughness: 0.35
    });
    const tubeMesh = new THREE.Mesh(tubeGeo, tubeMat);
    scene.add(tubeMesh);

    const pylon = new THREE.Mesh(new THREE.CylinderGeometry(0.9, 1.3, 16, 10), MAT.darkIron);
    pylon.position.set(midX, 8, midZ);
    scene.add(pylon);

    const podGrp = new THREE.Group();
    const podBody = new THREE.Mesh(new THREE.CylinderGeometry(1.6, 1.6, 5.5, 12), MAT.brushedSteel);
    podBody.rotation.z = Math.PI / 2;
    const podBand = new THREE.Mesh(new THREE.CylinderGeometry(1.75, 1.75, 2.2, 12), MAT.signalOrange);
    podBand.rotation.z = Math.PI / 2;
    podGrp.add(podBody, podBand);
    scene.add(podGrp);
    packetDrones.push({ mesh: podGrp, curve, offset: idx * 0.19, idA, idB });
  });

  // ==================== CAD TARGET RETICLE IN 3D SPACE ====================
  const reticleRing = new THREE.Mesh(
    new THREE.TorusGeometry(6.2, 0.42, 12, 32),
    new THREE.MeshBasicMaterial({ color: 0xf97316 })
  );
  reticleRing.rotation.x = Math.PI / 2;
  scene.add(reticleRing);

  // ==================== FLOATING 3D WORLD-SPACE CAD CALLOUT PINS ====================
  const pinsContainer = document.getElementById("pins-container");
  let lastPinZoneId = null;
  let lastPinSubIdx = -1;
  let pinElements = [];

  function updateWorldSpacePins(activeZone, activeSubIdx) {
    if (lastPinZoneId !== activeZone.id || lastPinSubIdx !== activeSubIdx) {
      lastPinZoneId = activeZone.id;
      lastPinSubIdx = activeSubIdx;
      pinsContainer.innerHTML = activeZone.subParts.map(sp => {
        const isFocus = sp.idx === activeSubIdx;
        return `
          <div data-sub="${sp.idx}" class="pin-3d ${isFocus ? "pin-active" : "pin-idle"}">
            <div class="pin-row">
              <span class="pin-num code-font">0${sp.idx + 1}</span>
              <span class="pin-title">${escapeHtml(sp.label)}</span>
            </div>
            <div class="pin-code code-font">${escapeHtml(sp.code)}</div>
          </div>
        `;
      }).join("");

      pinElements = Array.from(pinsContainer.querySelectorAll(".pin-3d"));
      pinElements.forEach(el => {
        el.addEventListener("click", (e) => {
          e.stopPropagation();
          const subIdx = parseInt(el.getAttribute("data-sub"), 10);
          const pl = PLAYLISTS[currentMode];
          const found = pl.findIndex(item => {
            const s = SCENES[item.sceneIndex];
            return s.zoneId === activeZone.id && s.subPartIdx === subIdx;
          });
          if (found >= 0) {
            currentStepIdx = found;
            cameraLocked = true;
            updateCameraBadge();
            syncUI();
          }
        });
      });
    }

    const w = window.innerWidth;
    const h = window.innerHeight;
    activeZone.subParts.forEach((sp, idx) => {
      const el = pinElements[idx];
      if (!el) return;
      const worldVec = new THREE.Vector3(
        activeZone.x + sp.dx,
        sp.dy + 13,
        activeZone.z + sp.dz
      );
      worldVec.project(camera);
      if (worldVec.z > 1.0) {
        el.style.display = "none";
        return;
      }
      const sx = (worldVec.x * 0.5 + 0.5) * w;
      const sy = (-worldVec.y * 0.5 + 0.5) * h;
      const minX = 395;
      const maxX = hudHidden ? w - 120 : (hudExpanded ? w - 680 : w - 510);
      if (sx < 180 || sx > w - 180 || sy < 50 || sy > h - 90) {
        el.style.display = "none";
      } else {
        el.style.display = "block";
        el.style.left = `${Math.max(minX, Math.min(maxX, sx))}px`;
        el.style.top = `${Math.max(78, Math.min(h - 140, sy))}px`;
      }
    });
  }

  // ==================== TOP-LEFT SCHEMATIC RADAR ====================
  const radarCanvas = document.getElementById("radar-canvas");
  const rCtx = radarCanvas.getContext("2d");

  function renderRadar(activeZone) {
    const W = radarCanvas.width;
    const H = radarCanvas.height;
    rCtx.save();
    rCtx.clearRect(0, 0, W, H);

    const mapX = x => W * 0.52 + (x / 420) * (W * 0.43);
    const mapZ = z => H * 0.50 + (z / 245) * (H * 0.39);

    rCtx.strokeStyle = "rgba(255, 255, 255, 0.04)";
    rCtx.lineWidth = 1;
    for (let gx = 0; gx < W; gx += 24) { rCtx.beginPath(); rCtx.moveTo(gx, 0); rCtx.lineTo(gx, H); rCtx.stroke(); }
    for (let gy = 0; gy < H; gy += 24) { rCtx.beginPath(); rCtx.moveTo(0, gy); rCtx.lineTo(W, gy); rCtx.stroke(); }

    rCtx.strokeStyle = "rgba(255, 255, 255, 0.18)";
    rCtx.lineWidth = 1.4;
    HIGHWAY_LINKS.forEach(([a, b]) => {
      const za = ZONES_3D.find(z => z.id === a);
      const zb = ZONES_3D.find(z => z.id === b);
      if (!za || !zb) return;
      rCtx.beginPath();
      rCtx.moveTo(mapX(za.x), mapZ(za.z));
      rCtx.lineTo(mapX(zb.x), mapZ(zb.z));
      rCtx.stroke();
    });

    ZONES_3D.forEach((z, idx) => {
      const zx = mapX(z.x);
      const zz = mapZ(z.z);
      const isAct = z.id === activeZone.id;
      rCtx.beginPath();
      rCtx.arc(zx, zz, isAct ? 6.0 : 3.8, 0, Math.PI * 2);
      rCtx.fillStyle = isAct ? "#f97316" : "#6b7280";
      rCtx.fill();
      if (isAct) {
        rCtx.strokeStyle = "#ffffff";
        rCtx.lineWidth = 1.8;
        rCtx.stroke();
      }
      rCtx.fillStyle = isAct ? "#ffffff" : "#9ca3af";
      rCtx.font = "bold 9.5px Consolas, monospace";
      rCtx.fillText(String(idx + 1).padStart(2, "0"), zx + 7, zz + 3);
    });

    const cx = mapX(camera.position.x);
    const cz = mapZ(camera.position.z);
    const lx = mapX(camCurrentLook.x);
    const lz = mapZ(camCurrentLook.z);
    rCtx.strokeStyle = "rgba(249, 115, 22, 0.75)";
    rCtx.beginPath();
    rCtx.moveTo(cx, cz);
    rCtx.lineTo(lx, lz);
    rCtx.stroke();
    rCtx.fillStyle = "#f97316";
    rCtx.beginPath();
    rCtx.arc(cx, cz, 3.0, 0, Math.PI * 2);
    rCtx.fill();

    rCtx.restore();
  }

  radarCanvas.addEventListener("click", e => {
    const rect = radarCanvas.getBoundingClientRect();
    const clickX = (e.clientX - rect.left) / rect.width;
    const clickY = (e.clientY - rect.top) / rect.height;
    const worldX = ((clickX - 0.52) / 0.43) * 420;
    const worldZ = ((clickY - 0.50) / 0.39) * 245;

    let bestZone = ZONES_3D[0];
    let bestDist = Infinity;
    ZONES_3D.forEach(z => {
      const d = Math.hypot(z.x - worldX, z.z - worldZ);
      if (d < bestDist) { bestDist = d; bestZone = z; }
    });

    const pl = PLAYLISTS[currentMode];
    const idx = pl.findIndex(item => SCENES[item.sceneIndex].zoneId === bestZone.id);
    if (idx >= 0) {
      currentStepIdx = idx;
      cameraLocked = true;
      updateCameraBadge();
      syncUI();
      snapCameraToActive();
    }
  });

  // ==================== 2D CAD LEADER LINE OVERLAY ====================
  const laserCanvas = document.getElementById("laser-canvas");
  const lCtx = laserCanvas.getContext("2d");

  function drawHudLaserBeam(activeZone, activeSubPart, t) {
    const w = window.innerWidth;
    const h = window.innerHeight;
    if (laserCanvas.width !== w || laserCanvas.height !== h) {
      laserCanvas.width = w;
      laserCanvas.height = h;
    }
    lCtx.clearRect(0, 0, w, h);
    if (hudHidden) return;

    const worldVec = new THREE.Vector3(
      activeZone.x + activeSubPart.dx,
      activeSubPart.dy * 0.55,
      activeZone.z + activeSubPart.dz
    );
    worldVec.project(camera);
    if (worldVec.z > 1.0) return;

    const tx = (worldVec.x * 0.5 + 0.5) * w;
    const ty = (-worldVec.y * 0.5 + 0.5) * h;

    const hudEl = document.getElementById("xray-code-line");
    if (!hudEl) return;
    const rect = hudEl.getBoundingClientRect();
    const sx = rect.left;
    const sy = rect.top + rect.height * 0.5;

    const elbowX = sx - 28;
    lCtx.save();
    lCtx.beginPath();
    lCtx.moveTo(sx, sy);
    lCtx.lineTo(elbowX, sy);
    lCtx.lineTo(tx, ty);
    lCtx.strokeStyle = "rgba(249, 115, 22, 0.72)";
    lCtx.lineWidth = 1.6;
    lCtx.setLineDash([5, 4]);
    lCtx.lineDashOffset = -t * 28;
    lCtx.stroke();

    lCtx.setLineDash([]);
    lCtx.beginPath();
    lCtx.arc(tx, ty, 7 + Math.sin(t * 5) * 1.5, 0, Math.PI * 2);
    lCtx.strokeStyle = "#f97316";
    lCtx.lineWidth = 1.8;
    lCtx.stroke();

    lCtx.fillStyle = "#f97316";
    lCtx.beginPath();
    lCtx.arc(tx, ty, 2.5, 0, Math.PI * 2);
    lCtx.fill();
    lCtx.restore();
  }

  // ==================== UI SYNCHRONIZATION ====================
  function syncUI(overrideLine = null) {
    const pl = PLAYLISTS[currentMode];
    const { step, sc, zone, subPart } = getActiveZoneAndSubPart();
    const activeLine = overrideLine !== null ? overrideLine : step.lineNum;
    activeLineOverride = activeLine;

    let activeFileObj = sc.kind === "code_block" ? sc.file : DATA.files[step.overrideFileIndex ?? sc.fileIndex];
    document.getElementById("top-act-badge").textContent = sc.actLabel;
    document.getElementById("top-thread-badge").textContent = activeFileObj ? activeFileObj.thread : zone.thread;
    document.getElementById("top-machine-title").textContent = `${zone.name}  /  0${subPart.idx + 1}. ${subPart.label}`;
    document.getElementById("top-step-counter").textContent = `STEP ${String(currentStepIdx + 1).padStart(3, "0")} / ${pl.length}`;
    document.getElementById("top-file-badge").textContent = activeFileObj ? `${activeFileObj.path}:${activeLine}` : zone.short;

    const pct = pl.length > 1 ? ((currentStepIdx + 1) / pl.length) * 100 : 100;
    document.getElementById("scrubber-fill").style.width = `${pct.toFixed(2)}%`;

    document.getElementById("subtitle-scene-tag").textContent = `SCENE ${sc.sceneIndex + 1} / ${SCENES.length}`;
    if (sc.kind === "flow") {
      const fStepIdx = step.flowStepIndex || 0;
      const fStep = sc.flow.steps[fStepIdx];
      document.getElementById("subtitle-heading").textContent =
        `${sc.flow.title} — Hop ${fStepIdx + 1}/${sc.flow.steps.length}: ${fStep.target}`;
      document.getElementById("subtitle-body").innerHTML =
        `<strong style="color:#f3f4f6;">${formatInlineCode(fStep.actor)}</strong>` +
        (fStep.packet ? ` <span style="color:#fdba74;">▸ ${formatInlineCode(fStep.packet)}</span>` : "") +
        `<br>${formatInlineCode(fStep.detail)}`;
    } else {
      document.getElementById("subtitle-heading").textContent = `${sc.file.path} — ${sc.block.name}`;
      document.getElementById("subtitle-body").innerHTML =
        `${formatInlineCode(sc.block.summary)} <strong style="color:#fbbf24;margin-left:6px;">Invariant:</strong> ${formatInlineCode(sc.block.risk)}`;
    }

    renderTriggerButtons(zone);
    renderZoneWarpPills(zone.id, sc.kind === "flow");
    renderCodeHud(activeFileObj, sc, activeLine);
  }

  function renderTriggerButtons(zone) {
    const bar = document.getElementById("trigger-buttons-bar");
    bar.innerHTML = zone.triggers.map(tr => {
      const active = activeSimMode === tr.id;
      const cls = tr.danger
        ? (active ? "trig-btn trig-danger-active" : "trig-btn trig-danger")
        : (active ? "trig-btn trig-normal-active" : "trig-btn trig-normal");
      return `<button data-trig="${tr.id}" class="${cls}">${escapeHtml(tr.label)}</button>`;
    }).join("");

    const currentTrig = zone.triggers.find(t => t.id === activeSimMode) || zone.triggers[0];
    const statusPill = document.getElementById("sim-status-pill");
    statusPill.textContent = currentTrig.status;
    statusPill.style.color = currentTrig.danger ? "#fca5a5" : "#6ee7b7";

    bar.querySelectorAll(".trig-btn").forEach(btn => {
      btn.addEventListener("click", () => {
        activeSimMode = btn.getAttribute("data-trig");
        renderTriggerButtons(zone);
      });
    });
  }

  function renderZoneWarpPills(activeZoneId, isAct1) {
    const container = document.getElementById("chapter-pills");
    const act1Btn = `<button data-warp="act1" class="warp-pill code-font ${isAct1 ? "warp-active" : ""}" style="grid-column:span 2;">00 // Act I: 8 Packet Missions</button>`;
    const zoneBtns = ZONES_3D.map(z => {
      const active = !isAct1 && z.id === activeZoneId;
      return `<button data-warp="${z.id}" class="warp-pill code-font ${active ? "warp-active" : ""}">${escapeHtml(z.short)}</button>`;
    }).join("");
    container.innerHTML = act1Btn + zoneBtns;

    container.querySelectorAll(".warp-pill").forEach(btn => {
      btn.addEventListener("click", () => {
        const target = btn.getAttribute("data-warp");
        const pl = PLAYLISTS[currentMode];
        if (target === "act1") {
          currentStepIdx = 0;
        } else {
          const idx = pl.findIndex(item => {
            const s = SCENES[item.sceneIndex];
            return s.kind === "code_block" && s.zoneId === target;
          });
          if (idx >= 0) currentStepIdx = idx;
        }
        activeSimMode = "normal";
        cameraLocked = true;
        updateCameraBadge();
        syncUI();
        snapCameraToActive();
      });
    });
  }

  function renderCodeHud(fileObj, sc, activeLine) {
    if (!fileObj) return;
    const clampedLine = Math.max(1, Math.min(fileObj.lineCount, activeLine || 1));
    const lineData = fileObj.lines[clampedLine - 1];

    document.getElementById("hud-file-title").textContent = fileObj.path;
    const startL = sc.kind === "flow" ? Math.max(1, clampedLine - 4) : sc.startLine;
    const endL = sc.kind === "flow" ? Math.min(fileObj.lineCount, clampedLine + 10) : sc.endLine;
    document.getElementById("hud-line-range").textContent = `L${startL}–${endL}`;

    document.getElementById("xray-line-badge").textContent = `LINE ${lineData[0]}`;
    const pinBadge = document.getElementById("xray-pinned-badge");
    if (lineData[5] === 1) pinBadge.classList.remove("hidden");
    else pinBadge.classList.add("hidden");

    document.getElementById("xray-code-line").textContent =
      `${String(lineData[0]).padStart(3, " ")} | ${lineData[1] || "// (blank line)"}`;
    document.getElementById("xray-what").innerHTML = formatInlineCode(lineData[2]);
    document.getElementById("xray-low").innerHTML = formatInlineCode(lineData[3]);
    document.getElementById("xray-why").innerHTML = formatInlineCode(lineData[4]);

    const listEl = document.getElementById("hud-code-list");
    const rows = [];
    for (let ln = startL; ln <= endL; ln++) {
      const item = fileObj.lines[ln - 1];
      if (!item) continue;
      const isCur = ln === clampedLine;
      const isPin = item[5] === 1;
      rows.push(`
        <div data-ln="${ln}" class="hud-line-row ${isCur ? "hud-line-active" : isPin ? "hud-line-pin" : ""}">
          <span class="hud-ln code-font">${ln}</span>
          <span class="hud-star">${isPin ? "★" : ""}</span>
          <pre class="hud-code code-font">${escapeHtml(item[1])}</pre>
        </div>
      `);
    }
    listEl.innerHTML = rows.join("");

    listEl.querySelectorAll(".hud-line-row").forEach(r => {
      r.addEventListener("click", () => {
        const ln = parseInt(r.getAttribute("data-ln"), 10);
        syncUI(ln);
      });
    });

    const activeRow = listEl.querySelector(`[data-ln="${clampedLine}"]`);
    if (activeRow) activeRow.scrollIntoView({ block: "nearest", behavior: "smooth" });
  }

  // ==================== MASTER 60 FPS THREE.JS ANIMATION LOOP ====================
  const clock = new THREE.Clock();

  function animate() {
    requestAnimationFrame(animate);
    const t = clock.getElapsedTime();

    const { zone: activeZone, subPart: activeSubPart, subIdx: activeSubIdx } = getActiveZoneAndSubPart();

    globalSpinFans.forEach(fan => { fan.rotation.z = t * 14; });

    // 1. WASD + QE Free-Roam Flight
    const speed = 2.8;
    if (keysPressed["KeyW"] || keysPressed["KeyS"] || keysPressed["KeyA"] || keysPressed["KeyD"] || keysPressed["KeyQ"] || keysPressed["KeyE"]) {
      if (cameraLocked) {
        cameraLocked = false;
        updateCameraBadge();
      }
      const forward = new THREE.Vector3().subVectors(camCurrentLook, camCurrentPos).setY(0).normalize();
      const right = new THREE.Vector3().crossVectors(forward, new THREE.Vector3(0, 1, 0)).normalize();

      if (keysPressed["KeyW"]) { camCurrentPos.addScaledVector(forward, speed); camCurrentLook.addScaledVector(forward, speed); }
      if (keysPressed["KeyS"]) { camCurrentPos.addScaledVector(forward, -speed); camCurrentLook.addScaledVector(forward, -speed); }
      if (keysPressed["KeyD"]) { camCurrentPos.addScaledVector(right, speed); camCurrentLook.addScaledVector(right, speed); }
      if (keysPressed["KeyA"]) { camCurrentPos.addScaledVector(right, -speed); camCurrentLook.addScaledVector(right, -speed); }
      if (keysPressed["KeyE"]) { camCurrentPos.y += speed * 0.7; camCurrentLook.y += speed * 0.7; }
      if (keysPressed["KeyQ"]) { camCurrentPos.y = Math.max(10, camCurrentPos.y - speed * 0.7); }
    }

    // 2. Smooth Guided Camera with Frustum ViewOffset Centering
    if (cameraLocked) {
      if (!isDragging) {
        orbitYaw = 0.08 + Math.sin(t * 0.25) * 0.09;
      }
      const focusWeight = closeUpSubAssembly ? 0.90 : 0.0;
      const targetDist = closeUpSubAssembly ? 56 : orbitDist;
      const fx = activeZone.x + activeSubPart.dx * focusWeight;
      const fy = closeUpSubAssembly ? activeSubPart.dy * 0.7 : 12;
      const fz = activeZone.z + activeSubPart.dz * focusWeight;
      camTargetLook.set(fx, fy, fz);
      camTargetPos.set(
        fx + Math.sin(orbitYaw) * targetDist,
        fy + Math.sin(orbitPitch) * targetDist,
        fz + Math.cos(orbitYaw) * targetDist
      );
      camCurrentPos.lerp(camTargetPos, 0.12);
      camCurrentLook.lerp(camTargetLook, 0.14);
    }

    camera.position.copy(camCurrentPos);
    camera.lookAt(camCurrentLook);
    updateCameraFrustumOffset();

    activePointLight.position.set(
      activeZone.x + activeSubPart.dx,
      activeSubPart.dy + 34,
      activeZone.z + activeSubPart.dz
    );
    activePointLight.color.setHex(activeSimMode.startsWith("attack_") ? 0xef4444 : activeZone.color);

    reticleRing.position.set(
      activeZone.x + activeSubPart.dx,
      3.6,
      activeZone.z + activeSubPart.dz
    );
    reticleRing.rotation.z = t * 1.8;

    // 3. Animate All 10 Cutaway Hardware Stations & Live Fault Physics
    ZONES_3D.forEach(z => {
      const grp = machineGroups[z.id];
      if (!grp) return;
      const refs = grp.userData.animRefs;
      const isAct = z.id === activeZone.id;
      if (refs.rimMat) {
        refs.rimMat.emissiveIntensity = isAct ? 0.75 + Math.sin(t * 3) * 0.2 : 0.2;
      }

      if (z.id === "proto") {
        if (refs.pressHead) refs.pressHead.position.y = 11 + Math.abs(Math.sin(t * 3.5)) * 5.5;
        if (refs.byteCubes) {
          refs.byteCubes.forEach((c, i) => {
            c.position.y = 5.0 + Math.sin(t * 4.5 + i * 0.9) * 0.75;
          });
        }
        if (refs.attackCube) {
          refs.attackCube.visible = isAct && activeSimMode === "attack_4gb";
          if (refs.attackCube.visible) {
            refs.attackCube.position.y = 13 + Math.abs(Math.sin(t * 6)) * 6;
            refs.attackCube.rotation.y = t * 2.5;
          }
        }
        if (refs.gyroGroup) {
          refs.gyroGroup.children.forEach((ring, idx) => {
            if (idx < 3) {
              ring.rotation.x = t * (idx + 1) * 1.1;
              ring.rotation.y = t * (3 - idx) * 0.9;
            }
          });
        }
      }
      else if (z.id === "tls") {
        if (refs.stargateRings) {
          const isAtk = isAct && activeSimMode === "attack_tls12";
          refs.stargateRings.forEach((r, idx) => {
            r.rotation.z = t * (idx % 2 === 0 ? 1.8 : -1.8);
            r.material.color.setHex(isAtk ? 0xef4444 : 0x10b981);
            r.material.emissive.setHex(isAtk ? 0xdc2626 : 0x059669);
          });
        }
        if (refs.sanScannerHead) refs.sanScannerHead.position.x = Math.sin(t * 3.0) * 7.5;
        if (refs.gearCert) refs.gearCert.rotation.z = t * 1.5;
        if (refs.gearKey) refs.gearKey.rotation.z = -t * 1.5 + 0.26;
        if (refs.switchCyl) refs.switchCyl.rotation.y = Math.sin(t * 1.8) * 0.55;
      }
      else if (z.id === "server") {
        if (refs.strandOrbs) {
          refs.strandOrbs.forEach((sled, idx) => {
            sled.position.x = -22 + (((t * 14) + idx * 9.5) % 44);
          });
        }
        if (refs.weakRing) refs.weakRing.rotation.y = t * 2.2;
      }
      else if (z.id === "session") {
        if (refs.tankDiscs) {
          const isOver = isAct && activeSimMode === "attack_overflow";
          refs.tankDiscs.forEach((d, idx) => {
            d.position.y = 4.2 + idx * 3.2 + Math.sin(t * 4 + idx) * 0.4;
            d.material.color.setHex(isOver ? 0xef4444 : 0x38bdf8);
            d.material.emissive.setHex(isOver ? 0xdc2626 : 0x0284c7);
          });
        }
        if (refs.piston) refs.piston.position.x = -1 + Math.sin(t * 5.5) * 4.2;
        if (refs.pumpGear) refs.pumpGear.rotation.z = -t * 4;
        if (refs.clockGear) refs.clockGear.rotation.z = t * 2;
        if (refs.clockGear2) refs.clockGear2.rotation.z = -t * 2.4;
        if (refs.clockHand) refs.clockHand.rotation.z = -t * 2.5;
      }
      else if (z.id === "hub") {
        if (refs.plasmaArch) refs.plasmaArch.rotation.z = t * 2.5;
        if (refs.radarDish) refs.radarDish.rotation.y = t * 1.8;
        if (refs.blastDoor) {
          refs.blastDoor.position.y = 8.5 + Math.abs(Math.sin(t * 2.2)) * 5.5;
        }
      }
      else if (z.id === "sqlite") {
        if (refs.vaultDoor) refs.vaultDoor.rotation.y = Math.sin(t * 1.4) * 0.45;
        if (refs.millGear1) refs.millGear1.rotation.z = t * 2.2;
        if (refs.millGear2) refs.millGear2.rotation.z = -t * 2.2;
        if (refs.shaCrystal) {
          refs.shaCrystal.rotation.y = t * 2.4;
          refs.shaCrystal.rotation.x = t * 1.6;
        }
        if (refs.walPlatters) {
          refs.walPlatters.forEach((p, idx) => { p.rotation.y = t * (idx % 2 === 0 ? 2.8 : -2.8); });
        }
        if (refs.hddArms) {
          refs.hddArms.forEach((arm, idx) => { arm.rotation.y = -0.35 + Math.sin(t * 4 + idx) * 0.18; });
        }
      }
      else if (z.id === "client") {
        if (refs.railSled) refs.railSled.position.x = -9 + ((t * 24) % 18);
        if (refs.prism) refs.prism.rotation.y = t * 1.6;
        if (refs.brakeDisc) refs.brakeDisc.rotation.z = t * 3.0;
      }
      else if (z.id === "worker") {
        if (refs.bridge) {
          const drop = isAct && activeSimMode === "wifi_drop" && Math.sin(t * 2.5) < 0;
          refs.bridge.position.y = drop ? 2.5 : 8;
          refs.bridge.rotation.z = drop ? -0.28 : 0;
        }
        if (refs.drone) {
          refs.drone.position.x = Math.sin(t * 2.6) * 12;
          refs.drone.position.y = 21 + Math.cos(t * 5) * 2.2;
        }
        if (refs.droneRotors) {
          refs.droneRotors.forEach(blade => { blade.rotation.y = t * 22; });
        }
      }
      else if (z.id === "model") {
        if (refs.scaleBeam) {
          const tilt = (isAct && activeSimMode === "draft_save") ? 0.25 : Math.sin(t * 2) * 0.08;
          refs.scaleBeam.rotation.z = tilt;
        }
        if (refs.asrsCrane) {
          refs.asrsCrane.position.z = Math.sin(t * 2.0) * 7.5;
        }
        if (refs.roomCubes) {
          refs.roomCubes.forEach((rc, idx) => {
            rc.position.y = 6 + Math.sin(t * 2.5 + idx) * 1.2;
          });
        }
      }
      else if (z.id === "ui") {
        if (refs.guiGear) refs.guiGear.rotation.z = t * 3.5;
        if (refs.cmakePyr) refs.cmakePyr.rotation.y = Math.sin(t * 0.8) * 0.35;
      }
    });

    // 4. Move Data Capsule Pods along Elevated Conduits
    packetDrones.forEach(pd => {
      const u = (t * 0.30 + pd.offset) % 1;
      const pt = pd.curve.getPointAt(u);
      const tangent = pd.curve.getTangentAt(u);
      pd.mesh.position.copy(pt);
      pd.mesh.lookAt(pt.clone().add(tangent));
    });

    renderer.render(scene, camera);

    updateWorldSpacePins(activeZone, activeSubIdx);
    renderRadar(activeZone);
    drawHudLaserBeam(activeZone, activeSubPart, t);
  }

  // ==================== RAYCAST CLICKING & ORBIT CONTROLS ====================
  const raycaster = new THREE.Raycaster();
  const mouseNDC = new THREE.Vector2();
  let downPos = { x: 0, y: 0 };

  worldCanvas.addEventListener("mousedown", e => {
    isDragging = true;
    dragPrev = { x: e.clientX, y: e.clientY };
    downPos = { x: e.clientX, y: e.clientY };
  });

  window.addEventListener("mousemove", e => {
    if (!isDragging) return;
    const dx = e.clientX - dragPrev.x;
    const dy = e.clientY - dragPrev.y;
    dragPrev = { x: e.clientX, y: e.clientY };
    orbitYaw -= dx * 0.007;
    orbitPitch = Math.max(0.14, Math.min(1.30, orbitPitch + dy * 0.006));
    if (!cameraLocked) {
      const dist = camCurrentPos.distanceTo(camCurrentLook) || 120;
      camCurrentLook.x = camCurrentPos.x - Math.sin(orbitYaw) * dist;
      camCurrentLook.y = camCurrentPos.y - Math.sin(orbitPitch - 0.45) * dist;
      camCurrentLook.z = camCurrentPos.z - Math.cos(orbitYaw) * dist;
    }
  });

  window.addEventListener("mouseup", e => {
    isDragging = false;
    if (Math.hypot(e.clientX - downPos.x, e.clientY - downPos.y) < 5 && e.target === worldCanvas) {
      mouseNDC.x = (e.clientX / window.innerWidth) * 2 - 1;
      mouseNDC.y = -(e.clientY / window.innerHeight) * 2 + 1;
      raycaster.setFromCamera(mouseNDC, camera);
      const hits = raycaster.intersectObjects(clickableMeshes, false);
      if (hits.length > 0) {
        const { zoneId, subIdx } = hits[0].object.userData;
        if (zoneId) {
          const pl = PLAYLISTS[currentMode];
          const found = pl.findIndex(item => {
            const s = SCENES[item.sceneIndex];
            return s.zoneId === zoneId && (subIdx === undefined || s.subPartIdx === subIdx);
          });
          if (found >= 0) {
            currentStepIdx = found;
            cameraLocked = true;
            updateCameraBadge();
            syncUI();
          }
        }
      }
    }
  });

  worldCanvas.addEventListener("wheel", e => {
    e.preventDefault();
    orbitDist = Math.max(50, Math.min(420, orbitDist + e.deltaY * 0.14));
  }, { passive: false });

  window.addEventListener("resize", () => {
    renderer.setSize(window.innerWidth, window.innerHeight);
    updateCameraFrustumOffset();
  });

  // ==================== CONTROLS & EVENT WIRING ====================
  function updateCameraBadge() {
    const badge = document.getElementById("camera-mode-badge");
    if (cameraLocked) {
      badge.textContent = closeUpSubAssembly ? "CLOSE-UP LOCKED" : "TOUR LOCKED";
      badge.style.background = "rgba(249, 115, 22, 0.16)";
      badge.style.color = "#fdba74";
    } else {
      badge.textContent = "FREE CAM (WASD)";
      badge.style.background = "rgba(255, 255, 255, 0.08)";
      badge.style.color = "#f3f4f6";
    }
  }

  document.getElementById("btn-relock-camera").addEventListener("click", () => {
    cameraLocked = true;
    orbitDist = 114;
    orbitPitch = 0.46;
    updateCameraBadge();
    syncUI(activeLineOverride);
    snapCameraToActive();
  });

  const btnFocusZoom = document.getElementById("btn-focus-zoom");
  if (btnFocusZoom) {
    btnFocusZoom.addEventListener("click", () => {
      closeUpSubAssembly = !closeUpSubAssembly;
      cameraLocked = true;
      btnFocusZoom.textContent = closeUpSubAssembly ? "Full View" : "Close-Up";
      updateCameraBadge();
      snapCameraToActive();
    });
  }

  document.getElementById("btn-overview-camera").addEventListener("click", () => {
    cameraLocked = false;
    camCurrentPos.set(0, 390, 480);
    camCurrentLook.set(0, 0, 0);
    updateCameraBadge();
  });

  function stepForward() {
    const pl = PLAYLISTS[currentMode];
    if (currentStepIdx < pl.length - 1) {
      currentStepIdx++;
      activeSimMode = "normal";
      cameraLocked = true;
      updateCameraBadge();
      syncUI();
    } else if (autoPlayTimer) {
      toggleAutoPlay(false);
    }
  }

  function stepBackward() {
    if (currentStepIdx > 0) {
      currentStepIdx--;
      activeSimMode = "normal";
      cameraLocked = true;
      updateCameraBadge();
      syncUI();
    }
  }

  function stepNextScene() {
    const pl = PLAYLISTS[currentMode];
    const curSc = pl[currentStepIdx].sceneIndex;
    for (let i = currentStepIdx + 1; i < pl.length; i++) {
      if (pl[i].sceneIndex > curSc) {
        currentStepIdx = i;
        activeSimMode = "normal";
        cameraLocked = true;
        updateCameraBadge();
        syncUI();
        return;
      }
    }
  }

  function stepPrevScene() {
    const pl = PLAYLISTS[currentMode];
    const curSc = pl[currentStepIdx].sceneIndex;
    const targetSc = Math.max(0, curSc - 1);
    const idx = pl.findIndex(item => item.sceneIndex === targetSc);
    if (idx >= 0) {
      currentStepIdx = idx;
      activeSimMode = "normal";
      cameraLocked = true;
      updateCameraBadge();
      syncUI();
    }
  }

  function toggleAutoPlay(force = null) {
    const play = force !== null ? force : !autoPlayTimer;
    if (autoPlayTimer) { clearInterval(autoPlayTimer); autoPlayTimer = null; }
    document.getElementById("play-icon").textContent = play ? "⏸" : "▶";
    document.getElementById("play-label").textContent = play ? "Pause" : "Auto-Play";
    if (play) {
      const ms = parseInt(document.getElementById("autoplay-speed").value, 10) || 2800;
      autoPlayTimer = setInterval(stepForward, ms);
    }
  }

  document.getElementById("btn-next-step").addEventListener("click", stepForward);
  document.getElementById("btn-prev-step").addEventListener("click", stepBackward);
  document.getElementById("btn-next-scene").addEventListener("click", stepNextScene);
  document.getElementById("btn-prev-scene").addEventListener("click", stepPrevScene);
  document.getElementById("btn-play-pause").addEventListener("click", () => toggleAutoPlay());
  document.getElementById("autoplay-speed").addEventListener("change", () => {
    if (autoPlayTimer) toggleAutoPlay(true);
  });

  document.getElementById("btn-line-up").addEventListener("click", () => {
    const { step, sc } = getActiveZoneAndSubPart();
    const fObj = sc.kind === "code_block" ? sc.file : DATA.files[step.overrideFileIndex ?? sc.fileIndex];
    if (!fObj) return;
    const cur = activeLineOverride || step.lineNum || 1;
    if (cur > 1) syncUI(cur - 1);
  });
  document.getElementById("btn-line-down").addEventListener("click", () => {
    const { step, sc } = getActiveZoneAndSubPart();
    const fObj = sc.kind === "code_block" ? sc.file : DATA.files[step.overrideFileIndex ?? sc.fileIndex];
    if (!fObj) return;
    const cur = activeLineOverride || step.lineNum || 1;
    if (cur < fObj.lineCount) syncUI(cur + 1);
  });

  const hudWrapper = document.getElementById("code-hud-wrapper");
  const btnRestoreHud = document.getElementById("btn-hud-restore");

  function toggleHudHidden(force = null) {
    hudHidden = force !== null ? force : !hudHidden;
    if (hudHidden) {
      hudWrapper.classList.add("hidden");
      btnRestoreHud.classList.remove("hidden");
    } else {
      hudWrapper.classList.remove("hidden");
      btnRestoreHud.classList.add("hidden");
    }
    updateCameraFrustumOffset();
  }
  document.getElementById("btn-hud-collapse").addEventListener("click", () => toggleHudHidden(true));
  btnRestoreHud.addEventListener("click", () => toggleHudHidden(false));

  document.getElementById("btn-hud-size").addEventListener("click", e => {
    hudExpanded = !hudExpanded;
    hudWrapper.style.width = hudExpanded ? "620px" : "414px";
    e.currentTarget.textContent = hudExpanded ? "Compact" : "Expand";
    updateCameraFrustumOffset();
  });

  document.getElementById("scrubber-track").addEventListener("click", e => {
    const rect = e.currentTarget.getBoundingClientRect();
    const ratio = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    const pl = PLAYLISTS[currentMode];
    currentStepIdx = Math.floor(ratio * (pl.length - 1));
    cameraLocked = true;
    updateCameraBadge();
    syncUI();
    snapCameraToActive();
  });

  document.querySelectorAll(".mode-btn").forEach(btn => {
    btn.addEventListener("click", () => {
      const m = btn.getAttribute("data-mode");
      if (m === currentMode) return;
      const curSc = getCurrentStep().sceneIndex;
      currentMode = m;
      document.querySelectorAll(".mode-btn").forEach(b => {
        b.classList.toggle("mode-active", b.getAttribute("data-mode") === currentMode);
      });
      const newPl = PLAYLISTS[currentMode];
      const idx = newPl.findIndex(item => item.sceneIndex === curSc);
      currentStepIdx = idx >= 0 ? idx : 0;
      syncUI();
    });
  });

  window.addEventListener("keydown", e => {
    if (e.target && (e.target.tagName === "INPUT" || e.target.tagName === "SELECT")) return;
    keysPressed[e.code] = true;
    if (e.key === "ArrowRight" || e.key === " ") { e.preventDefault(); stepForward(); }
    else if (e.key === "ArrowLeft") { e.preventDefault(); stepBackward(); }
    else if (e.key === "PageDown" || e.key === "]") { e.preventDefault(); stepNextScene(); }
    else if (e.key === "PageUp" || e.key === "[") { e.preventDefault(); stepPrevScene(); }
    else if (e.key === "ArrowDown") { e.preventDefault(); document.getElementById("btn-line-down").click(); }
    else if (e.key === "ArrowUp") { e.preventDefault(); document.getElementById("btn-line-up").click(); }
    else if (e.key === "h" || e.key === "H") { e.preventDefault(); toggleHudHidden(); }
  });
  window.addEventListener("keyup", e => { keysPressed[e.code] = false; });

  // ==================== MODALS: NAVIGATOR & ORAL DEFENSE QUIZ ====================
  function jumpToFileAndLine(filePath, lineNum) {
    const fIdx = DATA.files.findIndex(f => f.path === filePath);
    if (fIdx < 0) return;
    const pl = PLAYLISTS[currentMode];
    let best = -1;
    for (let i = 0; i < pl.length; i++) {
      const sc = SCENES[pl[i].sceneIndex];
      if (sc.kind === "code_block" && sc.fileIndex === fIdx) {
        if (lineNum >= sc.startLine && lineNum <= sc.endLine) {
          best = i;
          if (pl[i].lineNum === lineNum) break;
        }
      }
    }
    if (best >= 0) {
      currentStepIdx = best;
      cameraLocked = true;
      updateCameraBadge();
      syncUI(lineNum);
      snapCameraToActive();
    }
  }

  const modalNav = document.getElementById("modal-navigator");
  document.getElementById("btn-open-navigator").addEventListener("click", () => {
    modalNav.classList.remove("hidden");
    renderNavigatorBody("");
    document.getElementById("nav-search-input").focus();
  });
  document.getElementById("btn-close-navigator").addEventListener("click", () => modalNav.classList.add("hidden"));
  document.getElementById("nav-search-input").addEventListener("input", e => renderNavigatorBody(e.target.value));

  function renderNavigatorBody(query) {
    const q = (query || "").trim().toLowerCase();
    const body = document.getElementById("nav-modal-body");
    if (!q) {
      const byMod = {};
      DATA.files.forEach(f => {
        if (!byMod[f.module]) byMod[f.module] = [];
        byMod[f.module].push(f);
      });
      body.innerHTML = Object.entries(byMod).map(([m, files]) => `
        <div class="modal-section">
          <div class="modal-sec-title">${escapeHtml(m)}</div>
          <div class="modal-grid">
            ${files.map(f => `
              <div class="modal-card">
                <div style="display:flex;justify-content:space-between;align-items:center;">
                  <strong class="code-font" style="color:#f3f4f6;">${escapeHtml(f.path)}</strong>
                  <span style="color:#9ca3af;font-size:11px;">${f.lineCount} lines</span>
                </div>
                <div style="display:flex;flex-wrap:wrap;gap:4px;margin-top:6px;">
                  ${f.blocks.map(b => `
                    <button data-jf="${escapeHtml(f.path)}" data-jl="${b.lines[0]}" class="warp-pill code-font">
                      L${b.lines[0]}–${b.lines[1]}: ${escapeHtml(b.name.slice(0, 26))}
                    </button>
                  `).join("")}
                </div>
              </div>
            `).join("")}
          </div>
        </div>
      `).join("");
    } else {
      const hits = [];
      for (const f of DATA.files) {
        for (const item of f.lines) {
          if (item[1].toLowerCase().includes(q) || item[2].toLowerCase().includes(q) || item[3].toLowerCase().includes(q)) {
            hits.push({ path: f.path, item });
            if (hits.length >= 50) break;
          }
        }
        if (hits.length >= 50) break;
      }
      body.innerHTML = hits.map(h => `
        <button data-jf="${escapeHtml(h.path)}" data-jl="${h.item[0]}" class="modal-card" style="width:100%;text-align:left;margin-bottom:6px;cursor:pointer;">
          <div class="code-font" style="color:#f97316;font-weight:bold;">${escapeHtml(h.path)}:${h.item[0]}</div>
          <pre class="code-font" style="color:#f3f4f6;margin:4px 0;overflow:hidden;text-overflow:ellipsis;">${escapeHtml(h.item[1])}</pre>
          <div style="color:#9ca3af;">${formatInlineCode(h.item[2])}</div>
        </button>
      `).join("");
    }
    body.querySelectorAll("[data-jf]").forEach(b => {
      b.addEventListener("click", () => {
        modalNav.classList.add("hidden");
        jumpToFileAndLine(b.getAttribute("data-jf"), parseInt(b.getAttribute("data-jl"), 10));
      });
    });
  }

  const modalExam = document.getElementById("modal-exam");
  document.getElementById("btn-open-exam").addEventListener("click", () => modalExam.classList.remove("hidden"));
  document.getElementById("btn-close-exam").addEventListener("click", () => modalExam.classList.add("hidden"));

  const ALL_PINNED = [];
  DATA.files.forEach(f => f.lines.forEach(item => { if (item[5] === 1) ALL_PINNED.push({ path: f.path, title: f.title, item }); }));
  let currentDrill = ALL_PINNED[0];

  function pickRandomDrill() {
    currentDrill = ALL_PINNED[Math.floor(Math.random() * ALL_PINNED.length)];
    document.getElementById("drill-location").textContent = `${currentDrill.path}:${currentDrill.item[0]} — ${currentDrill.title}`;
    document.getElementById("drill-code").textContent = `${currentDrill.item[0]} | ${currentDrill.item[1]}`;
    document.getElementById("drill-what").innerHTML = formatInlineCode(currentDrill.item[2]);
    document.getElementById("drill-low").innerHTML = formatInlineCode(currentDrill.item[3]);
    document.getElementById("drill-why").innerHTML = formatInlineCode(currentDrill.item[4]);
    document.getElementById("drill-answer").classList.add("hidden");
  }
  document.getElementById("btn-drill-reveal").addEventListener("click", () => document.getElementById("drill-answer").classList.toggle("hidden"));
  document.getElementById("btn-drill-next").addEventListener("click", pickRandomDrill);
  document.getElementById("btn-drill-jump").addEventListener("click", () => {
    modalExam.classList.add("hidden");
    jumpToFileAndLine(currentDrill.path, currentDrill.item[0]);
  });

  function refButtons(refs, prefix) {
    return refs.map(([p, ln]) =>
      `<button data-jf="${escapeHtml(p)}" data-jl="${ln}" class="warp-pill code-font">${prefix}${escapeHtml(p)}:${ln}</button>`
    ).join("");
  }

  function initExamLists() {
    document.getElementById("exam-req-list").innerHTML = DATA.requirements.map(r => `
      <div class="modal-card">
        <div>
          <span class="badge-cyan code-font">${escapeHtml(r.id)}</span>
          <strong style="color:#f3f4f6;margin-left:6px;">${escapeHtml(r.title)}</strong>
          ${r.points ? `<span style="color:#9ca3af;margin-left:6px;">${escapeHtml(r.points)}</span>` : ""}
          <p style="color:#d1d5db;margin:6px 0;">${formatInlineCode(r.how)}</p>
          ${r.defense ? `<p style="color:#e5e7eb;margin:6px 0;"><strong style="color:#fbbf24;">Defense: </strong>${formatInlineCode(r.defense)}</p>` : ""}
        </div>
        <div style="display:flex;gap:6px;flex-wrap:wrap;">
          ${refButtons(r.serverRefs, "Server: ")}
          ${refButtons(r.clientRefs, "Client: ")}
        </div>
      </div>
    `).join("");

    document.getElementById("exam-qa-list").innerHTML = DATA.defenseQa.map((qa) => `
      <div class="modal-card" style="margin-bottom:8px;">
        <strong style="color:#f3f4f6;">${escapeHtml(qa.q)}</strong>
        <p style="color:#d1d5db;margin-top:6px;">${formatInlineCode(qa.a)}</p>
        <div style="display:flex;gap:6px;flex-wrap:wrap;margin-top:6px;">${refButtons(qa.refs.slice(0, 4), "Jump: ")}</div>
      </div>
    `).join("");

    modalExam.querySelectorAll("[data-jf]").forEach(b => {
      b.addEventListener("click", () => {
        modalExam.classList.add("hidden");
        jumpToFileAndLine(b.getAttribute("data-jf"), parseInt(b.getAttribute("data-jl"), 10));
      });
    });
  }

  function runSelfTest() {
    const errors = [];
    let count = 0;
    const saved = { mode: currentMode, step: currentStepIdx };
    Object.keys(PLAYLISTS).forEach(mode => {
      currentMode = mode;
      for (let i = 0; i < PLAYLISTS[mode].length; i++) {
        currentStepIdx = i;
        try { syncUI(); count++; } catch (e) {
          if (errors.length < 10) errors.push(`${mode}#${i}: ${e.message}`);
        }
      }
    });
    currentMode = saved.mode;
    currentStepIdx = saved.step;
    syncUI();
    snapCameraToActive();
    console.log(errors.length === 0
      ? `SELFTEST OK steps=${count} scenes=${SCENES.length} meshes=${clickableMeshes.length}`
      : `SELFTEST FAIL ${errors.join(" | ")}`);
  }

  // ==================== STARTUP ====================
  try { syncUI(); snapCameraToActive(); } catch (e) { showFatal(e.stack || e.message); }
  animate();
  try { pickRandomDrill(); initExamLists(); } catch (e) { console.error("Quiz init failed:", e); }
  if (location.hash.includes("selftest")) runSelfTest();
})();
