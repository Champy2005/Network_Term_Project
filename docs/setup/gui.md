# Windows chat GUI

Build Debug using the [Windows setup](windows.md) or the editor's preparation
task. Run the server in one terminal, then open one client per username:

```powershell
python tools/dev_tls.py
build/debug/chat_server.exe --tls certs/development/server.pem certs/development/server.key 127.0.0.1 5555
build/debug/chat_gui.exe --tls certs/development/ca.pem
```

Generate certificates once; existing credentials are not overwritten. See
[TLS setup](../tls.md) for alternate paths, LAN SANs, and explicit loopback
development mode. The GUI verifies the Host field's DNS/IP SAN by default;
`--server-name` supplies an explicit expected identity. Certificate failures end
connection/recovery and keep controls responsive.

The server stays running until Ctrl+C. The GUI is a separate Windows program;
start another copy for each additional user. It uses native Unicode controls
and requires no additional GUI package. The model/worker are portable C++, but
this adapter and its hidden-window tests build only on Windows.

## Connection and rooms

Enter the server host, port, and an unused username, then choose **Connect**.
Names are case-sensitive, with 1-32 ASCII letters, digits, or underscores.
Connection/login rejection appears in the status line. After failure finishes,
edit the name/address and connect again. **Disconnect** requests stop; an active
operation can take up to its three-second deadline to finish. Network work runs
on a worker so connect/login/send do not block control-message processing.

After login the worker sends a heartbeat Ping every 10 seconds. A missing or
incorrect reply ends the connection and shows `Heartbeat failed:` in the status
line; sending is disabled and Connect becomes available after the worker finishes.
Window messages continue to run during that exchange. The server removes stale
presence/membership after 30 seconds without a complete request. See the
[heartbeat guide](../heartbeats.md) for configuration and timing limits.

- **Connected users** includes yourself. Select any connected name to open a
  direct room. Both participants receive the server echo/delivery; a third user
  receives no direct text.
- **Groups** lists every group and its ID, including groups you have not joined.
  Selecting one displays its members and its own room view. Names can repeat;
  IDs distinguish groups.
- Enter a group name and choose **Create group**. Initially only you are a
  member. Select its group entry to open it. Another user must select it and
  choose **Join selected group** before sending there.
- **Your room views** switches between separate transcripts and unsent drafts.
  Enter text and choose **Send**. Enter inserts a newline. Server acceptance
  clears the submitted draft only if you have not changed it meanwhile. Server
  deliveries populate the transcript. Embedded NUL bytes are displayed as
  `␀`, preserving the rest of the message. A rejected request keeps the draft and
  shows its reason in the status line.

The server enforces sender identity and membership independently of the GUI.
Disabled controls are only a convenience. The existing headless API tests also
attempt unauthorized sends and check the unrelated/nonmember socket.

## Limits and retention

Messages must contain 1-4,096 UTF-8 bytes; group names 1-64 bytes. Unicode can
use several bytes per character, so an oversized draft can remain editable
while Send rejects it. Local views keep up to 100 lines each and 2 MiB of
sender/text/receipt strings combined; oldest lines are discarded. At most 256 views
are retained, evicting old direct views first while preserving discovered
groups. Each draft is bounded to 16,384 UTF-8 bytes. These are local display
buffers, not a persistent server history.

Manual Disconnect leaves old views offline for reading. A successful fresh
Connect clears local transcripts/drafts and claims a new identity. A fresh peer
claiming an old name gets a separate direct room.

With a storage-enabled server, transport loss now shows **Reconnecting**, retains
the selected room/drafts/transcripts, and disables Send while the worker resumes
the same identity. It self-rejoins only previously chosen groups and replays
missing sequences before showing **Reconnected**. Original direct peers can
reactivate their existing room; a fresh namesake cannot. Pending message attempts
keep their original retry keys and complete after recovery, without duplicate
insertions or transcript lines. Disconnect interrupts retry backoff.

Tokens, cursors, and retry queues remain in memory only for the running worker;
application restart does not restore them. Ephemeral servers do not support
automatic recovery. Terminal failure or manual reconnect discards pending
attempts; manually sending an uncertain draft again uses a fresh key and may
duplicate earlier acceptance. There is no general history browser or immediate
quiet-group history loading. AI remains later work. See [recovery](../recovery.md),
[history](../history.md), and [message reliability](../reliability.md).

For eventual LAN use, bind the server to its LAN address or `0.0.0.0` and enter
that computer's reachable LAN IP in each client. Use `--tls` with a certificate
SAN matching that IP and distribute only the public CA certificate. Use the actual printed port
if the server selected port zero. The physical-machine run has not been tested.

## Verification status

Automated hidden-window tests exercise the actual controls, Unicode messages,
direct/group separation and drafts, explicit self-join, presence updates,
login rejection/retry, disconnection, missing-Pong status and reconnect controls,
responsiveness during a slow login or heartbeat exchange, and draft/transcript/
selected-group retention across automatic recovery, verified TLS login/send, and
certificate-name rejection with responsive controls.
Portable model/worker tests cover retention, queue limits, cancellation, and
three-client privacy. Results are recorded in [progress](../progress.md).

Manual visual layout/DPI, keyboard/focus/accessibility, three visible GUI-client
flows, and the two-physical-computer LAN demonstration remain **pending**. They
were deliberately left pending because manual testing was not requested now.
The later [acceptance script](../testing.md) describes those checks.
