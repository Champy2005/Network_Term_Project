# Persistent chat CLI and headless API

Stage 13 adds a persistent console client using the same `ChatWorker` as the
Windows GUI. It supports user/group discovery, private messages, creator-only
group creation, voluntary joining, and member-only group messages. Networking,
heartbeats, and durable recovery continue while input is idle.

## Run chat

After building Debug and generating development certificates once as described
in [TLS setup](tls.md), run the server and one client per user in separate terminals:

```powershell
build/debug/chat_server.exe --tls certs/development/server.pem certs/development/server.key 127.0.0.1 5555
build/debug/chat_cli.exe --tls certs/development/ca.pem chat 127.0.0.1 5555 Alice_01
build/debug/chat_cli.exe --tls certs/development/ca.pem chat 127.0.0.1 5555 Bob_02
```

Use the server's printed port if you selected zero. LAN use needs a matching
DNS/IP SAN and the public CA on each client. `--server-name` works as in the TLS
guide. For explicit local development only:

```powershell
build/debug/chat_server.exe --plaintext-dev 127.0.0.1 5555
build/debug/chat_cli.exe --plaintext-dev chat 127.0.0.1 5555 Alice_01
```

The executable syntax is:

```text
chat_cli <transport-mode> chat <host> <port> <username>
chat_cli <transport-mode> <host> <port> [nonce] [username]
```

The second form preserves the one-shot login/Ping diagnostic. The first logs in,
prints `CONNECTED`, and keeps the socket open. Login/certificate/connection
failures print a terminal reason and exit even if stdin remains open.

## Commands

| Command | Behavior |
| --- | --- |
| `/help` | Print command syntax. |
| `/users` | Show the latest connected-user snapshot, including yourself. |
| `/groups` | Show all discovered group IDs, names, and current members. |
| `/members group-ID` | Show the latest member list for that exact group ID. |
| `/dm Bob_02 message text` | Send private text to a connected user. |
| `/create Study group` | Create a group with that spaced name; only you join initially. |
| `/join group-ID` | Request your own membership. |
| `/group group-ID message text` | Send to a group; server membership is required. |
| `/quit` | Stop input, drain admitted operation receipts, then disconnect. |

Copy the opaque ID printed in `OK ... create room="..."` or `GROUP` when joining
or sending. Group names can repeat; names are never substituted for IDs. Lists
are cached informational snapshots. The server authorizes every operation,
including requests that disagree with the cached view.

Commands are single lines. Leading separators are skipped; the remainder after
the target is literal text, preserving internal and trailing spaces. `/create`
takes the remaining line as its name. Quotes, backticks, and dollar signs have
no special interpretation; there is no shell evaluation or backslash expansion.
Press Enter to submit. The native Windows adapter supports Unicode input,
basic backspace, and Ctrl+D/Ctrl+Z EOF. Its interactive appearance and keyboard
behavior still need manual acceptance.

## Output and receipts

Snapshots print `USERS`, `GROUPS`, and `GROUP ... MEMBERS`. Incoming deliveries
print `DM` or `GROUP_MESSAGE`, with the server room ID, sequence, sender, and
text. Updates print while stdin is idle; there is no extra blocking input thread.

`QUEUED` is local admission. `OK` is the correlated server receipt; `REJECTED`
includes the typed server error code and reason. Receipt output does not insert
an optimistic message line: the server echo/replay prints the delivery. An ACK
means server acceptance, not recipient display or exactly-once delivery. See
[message reliability](reliability.md). Output escapes ASCII controls, quotes,
and backslashes, so a message cannot inject an ANSI escape or extra status line.
Resume capabilities and retry keys are never printed or written to disk.

Any syntax/admission/server rejection or terminal failure latches exit code one;
valid commands can still follow an error. A clean session ends with code zero.
Exit status therefore also works for piped command scripts.

## Input, ownership, and exit limits

Stdin is polled on the presentation thread, separately from the single networking
owner. Windows uses ready console events or pipe/file bytes; POSIX uses ready
stdin bytes. Native handling follows Microsoft's
[console input](https://learn.microsoft.com/en-us/windows/console/readconsoleinput)
and [pipe polling](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-peeknamedpipe)
APIs. Pipe/file input is UTF-8. A read admits at most 1,024 bytes; the partial
line is bounded to 8,192 bytes. Oversized lines are discarded through their
delimiter and produce an error, so their suffix cannot become a new command.
CR, LF, split CRLF, and an unterminated final line at EOF are supported.

The console tracks at most 64 pending receipts; worker command/update, text,
group, and recovery bounds remain unchanged. It retains only current snapshots
and pending IDs, without a transcript or disk log. Slow stdout can stall the
presentation loop; the bounded worker then fails explicitly on update overflow.

`/quit` and EOF stop new input and allow at most five seconds for admitted
receipts. After that they request worker cancellation and report uncertain
operations with exit code one. The current operation has its own three-second
deadline, so joining can take that additional bounded time. Later input is
ignored. Ctrl+C/SIGTERM requests immediate cancellation, ending backoff; active
socket work still finishes under its deadline. Terminal failures never wait
for another input line or for an uncancellable stdin thread.

Durable transport loss prints `RECONNECTING`, retains admitted message attempts,
and resumes the same identity with saved keys and chosen-group rejoin. Sequence
catch-up prints missed deliveries once before `RECONNECTED`; certificate errors
are terminal. New sends during recovery are rejected locally; `/quit` remains
available. An uncertain create operation is not blindly retried. Process restart
saves no token, pending key, or replay cursor. See [recovery](recovery.md).

## API boundary and verification

Headless callers include `chat/client/chat_client.hpp` and link `chat::transport`.
The blocking API exposes plain operation/event/history/sync types and verified
TLS options; callers do not handle Boost.Asio, frame bytes, or Protobuf. Use one
caller per client instance and schedule its heartbeat/event reads, or use the
bounded `ChatWorker` for background ownership and recovery. Unknown `CommandKind`
values are rejected at admission.

```powershell
python tools/check_cli_apps.py
python tools/check_tls_apps.py
```

The automated CLI suite covers syntax/byte bounds, terminal escaping, fragmented
and oversized lines, EOF, real TLS command/event flows, three-client privacy,
creator-only groups, nonmember denial, join/send, login/certificate failure,
receipt draining/deadlines, unknown worker actions, encrypted replay, and quit
during backoff. The process smoke tests drive three real executables in both
transport modes, with idle open stdin, Unicode messages, EOF, and terminal
failure. See [progress](progress.md) for actual preset results. Interactive
terminal editing/display, Ctrl+C behavior, editor/browser walkthroughs, and
two-physical-computer acceptance remain pending.
