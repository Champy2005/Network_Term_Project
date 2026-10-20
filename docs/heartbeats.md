# Heartbeats and stale sessions

Stage 9 uses the existing v1 `Ping`/`Pong` messages to keep quiet clients alive
and detect a peer that stops replying. The Windows GUI's networking worker
sends a Ping every 10 seconds after successful login. The server expires any
connection after 30 seconds without a complete inbound request, including
connections that have not logged in. No new Protobuf fields or frame rules
are introduced.

## What counts as activity

The server refreshes activity only after receiving a complete, parsed envelope
with a supported version and client-request payload. A Ping works before or
after login; ordinary login, chat, group, and history requests also refresh
activity, including well-formed requests the hub rejects. Bytes from an
incomplete frame, outgoing messages, snapshots, and socket writes do not
refresh it. A read-only consumer must still send Pings: receiving other users'
messages alone does not keep its server session alive.

The worker uses unique request IDs and increasing nonces; the headless client's
existing validation requires the Pong to match both. A missing, incorrect, or
failed heartbeat response closes the socket and publishes a terminal failure
starting with `Heartbeat failed:`. Pongs are consumed internally. Interleaved
chat events still enter the bounded update queue. The GUI shows the reason,
disables sending, and enables Connect when the worker finishes. If the event
poll detects EOF first, the existing connection error may appear instead.

Expiration follows the existing close path: cancel socket work, discard pending
writes, release the active username and live group memberships, refresh other
clients' snapshots, and free the connection slot. Stored identity, messages,
and groups remain in the database. A later login still requires explicit group
rejoin. Automatic reconnect and history catch-up remain future stages.

## Configuration

The server program accepts an optional fourth argument in milliseconds:

```powershell
build/debug/chat_server.exe --plaintext-dev 127.0.0.1 5555 data/chat.sqlite3 30000
```

The argument must be an integer from 0 through 86,400,000. Zero explicitly
disables idle expiry. Existing commands without the argument use 30 seconds.
Use `:memory:` as the database argument for isolated experiments.

Embedded servers accept `SessionLimits` as the fourth constructor argument;
the GUI-independent worker accepts `WorkerLimits`. For example:

```cpp
chat::server::SessionLimits sessions;
sessions.idle_timeout = std::chrono::seconds(30);
chat::server::PingServer server(context, endpoint, ":memory:", sessions);

chat::client::WorkerLimits worker_limits;
worker_limits.heartbeat_interval = std::chrono::seconds(10);
worker_limits.operation_timeout = std::chrono::seconds(3);
chat::client::ChatWorker worker(worker_limits);
```

Both timer budgets allow zero to disable their respective behavior and reject
negative values or values above 24 hours. Other existing queue and operation
bounds must remain positive. Changing the server timeout does not negotiate a
new client cadence. Keep the heartbeat interval plus the bounded operation
deadline comfortably below the server timeout. The shipped GUI uses the
defaults; shorter server timeouts require a correspondingly configured worker.

The blocking `ChatClient` does not create a background thread. Applications
using it directly must call `ping` periodically from the same owner that calls
other operations. Each instance still has one caller and one ordered write
path. A `next_event` timeout remains soft; the application must schedule its
own heartbeat between polls. The diagnostic CLI sends one Ping and exits.

## Scheduling and cancellation

Each server session owns one steady-clock timer on the same executor as its
socket and hub. One pending wait holds the session alive. At wakeup it checks
the latest activity and either closes or schedules the remaining interval.
Complete requests update the timestamp without creating a wait per message.
Stop cancels the timer and socket work; the caller drains the context to release
captured ownership. Clean read EOF can drain admitted replies only until idle
expiry. Boost.Asio documents these timer ownership and cancellation operations
in its [steady_timer reference](https://www.boost.org/doc/libs/latest/doc/html/boost_asio/reference/steady_timer.html).

The worker checks its heartbeat deadline before taking the next queued command,
and caps event polling at the remaining heartbeat interval. Sustained command
or snapshot traffic cannot indefinitely postpone a Ping. An already active
operation can postpone it until that operation's deadline. Disconnect requests
stop; it lets the active operation finish or time out, then starts no further
command or poll. Destruction joins that bounded operation. No UI thread accesses
the worker's socket, and timer work does not introduce a second writer.

These are executor deadlines, not hard real-time guarantees. The hub currently
performs synchronous SQLite commits; slow storage or a stalled executor can
delay both request processing and timer callbacks. Activity is not a delivery
receipt or a retry guarantee. Stage 10 separately defines
[message retries](reliability.md) and [automatic recovery](recovery.md).
[TLS](tls.md) adds verified network transport and an independent prelogin handshake
deadline; the example above uses explicit loopback development. Physical-network
acceptance remains pending.

## Automated evidence and pending checks

Fourteen socket/worker cases cover idle registered and unregistered peers,
name/member cleanup and slot reuse, partial-frame trickling, valid Ping and
chat activity, outbound-only traffic, pending read/write/timer cancellation,
correlation, silent/incorrect Pongs, stop during heartbeat, sustained commands,
disabled timers, and invalid budgets. A hidden native-control case checks the
heartbeat failure status, responsive window messages, and reconnect controls.

Debug, Release, ASAN, clangd, and documentation results are recorded in
[progress](progress.md). Manual visible-GUI checks, editor navigation, and a
two-physical-computer network interruption walkthrough remain pending.
