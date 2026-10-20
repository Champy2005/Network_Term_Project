# Persistent room history

Stage 8 stores accepted messages in SQLite and exposes authorized, paginated
reads through the headless client API. The Windows GUI still uses its local
bounded transcripts with automatic recovery replay; see [recovery](recovery.md) for
identity resume, sequence paging, and the limits of GUI catch-up.

## Running the server

The existing command now creates/opens `data/chat.sqlite3`:

```powershell
build/debug/chat_server.exe --plaintext-dev 127.0.0.1 5555
```

These examples use explicit loopback development mode. For network use, replace
`--plaintext-dev` with `--tls <certificate-chain> <key>`; see [TLS](tls.md).
To choose another file, provide its path after the bind address and port. Its parent
directory must already exist. The special `:memory:` path enables the storage
API without surviving process exit:

```powershell
build/debug/chat_server.exe --plaintext-dev 127.0.0.1 5555 C:/chat-data/project.sqlite3
build/debug/chat_server.exe --plaintext-dev 127.0.0.1 5555 :memory:
```

Do not start two servers on the same database; live membership/name registries
belong to one process. Embedded tests/applications can omit the `PingServer`
database argument for the original ephemeral mode, which rejects history and
resume requests. Invalid files or unsupported schema versions fail startup.

## Participant identity and access

A fresh storage-enabled login issues `LoginResult.resume_token`. Save it only
in private application storage if explicit identity resume is needed; do not
print it, commit it, or put it in command-line arguments. Only a SHA-256 hash
is stored in the database. Losing the token loses that participant's ability
to resume old direct-room access. Tokens bind a durable principal to its
username, while the active name must still be unique.

A name-only login after disconnect creates a new principal. It cannot inherit
the old user's direct history by choosing the same display name. Direct rooms
belong to their durable participant pair; either original participant may read
while the other is offline. Both original participants resuming reuse that
pair's room when they send again. This explicit capability resume is separate
from a fresh name claim. The worker retains this capability in memory for automatic
recovery; headless callers schedule resume themselves.

Group names/IDs survive restart. Connected memberships do not: everyone must
self-join again, including the creator. The worker repeats only its user's previous
create/join choices during automatic recovery. A current member can read
the group's full retained history, including earlier messages. Groups are
publicly discoverable and voluntarily joinable, as before. Stored admissions
record who joined; they do not restore active membership or authorize reads.

Tokens are bearer secrets protected in transit by verified TLS. Explicit
plaintext development is restricted to loopback. They prove possession of an
application capability, not a real-world identity; see [TLS configuration](tls.md).

## Headless API

Include `chat/client/chat_client.hpp` and link `chat::transport`. Keep calls
on one owner thread per instance, or use a dedicated worker. For example,
after connecting Alice's socket and logging in Bob on another client:

```cpp
const auto identity = alice.login("alice");
// Retain identity.resume_token privately if explicit resume is needed.
const auto sent = alice.send_direct("bob", "hello");
if (sent.accepted) {
    auto page = alice.history(sent.resource_id, 0, 50);
    // Inspect page.accepted/error before using page.records.
    // Read the next page with page.next_after_id when page.has_more is true.
}

// After reconnecting the socket, prove this earlier participant identity:
alice.close();
alice.connect("127.0.0.1", 5555);
const auto resumed = alice.resume_login("alice", identity.resume_token);
```

`history(room_id, after_id, limit, timeout)` returns a `HistoryPage` containing
ascending `HistoryRecord` values, `next_after_id`, `has_more`, and a group flag.
The cursor is an opaque database message ID, not a live room sequence. Zero
limit selects 50; the maximum is 100. Cursor values must fit a signed 64-bit
SQLite integer. Empty pages preserve the requested cursor. Authorization and
storage failures return typed errors. Malformed responses close the client.
Interleaved unsolicited events remain available through `next_event`.

## Storage and acceptance

| Table | Stored state |
| --- | --- |
| `users` | Durable principal ID, display name, and resume-token hash. Names can recur under different principals. |
| `rooms` | Durable direct participant pairs or group IDs/names and committed per-room sequence counter. |
| `admissions` | Historical voluntary group admissions by principal. |
| `messages` | Immutable sender/recipient/text, principal, room, global history cursor, sequence/timestamp, and optional retry key. |

Schema version is 2 (`PRAGMA user_version`), with an atomic upgrade from v1.
Old cursors/content remain intact; room sequences are backfilled in cursor order
and legacy timestamps are zero. New history records include message IDs,
room sequences, and timestamps. Foreign keys, a `(room,id)` index, and unique
sequence/retry indexes enforce relationships and support bounded pages. File stores require
WAL and use `synchronous=FULL`. SQLite describes WAL and its commit-sync
behavior in the [WAL documentation](https://www.sqlite.org/wal.html).
OpenSSL generates resume capabilities using
[RAND_bytes](https://docs.openssl.org/3.6/man3/RAND_bytes/).

Commit succeeds before an ACK or delivery is queued. Direct room creation
and its first message form one transaction; group creation and creator
admission also form one transaction. A failed write returns an error and
does not deliver uncommitted text. This does not guarantee recipient receipt,
exactly-once delivery or recovery without gaps. Stage 10 now assigns stable IDs
and sequences and deduplicates explicit saved-key retries without redelivery.
Message insertion, the key, and room-counter advancement commit in one
transaction; a failed update also rolls back the message. See
[message reliability](reliability.md) for the exact policy and migration behavior.

The single store runs on the hub strand. Lock waits use a 100 ms busy timeout;
disk flushes can still delay hub processing. Message storage grows with use;
there is no archival/retention policy yet. The database contains chat content.
Keep its files private and out of version control. The default directory and
SQLite files are ignored. Stop the server before copying the database and any
remaining `-wal`/`-shm` companions as one backup set; never delete a live WAL.

## Verification

Automated tests cover restart, original-token resume and same-name replacement
denial, group rejoin/history policy, committed reads from another connection,
bounded pagination, Unicode/NUL text, lock and insert failures, failed
admissions, read failures, invalid startup, and malformed/interleaved responses.
Debug/Release/ASAN evidence is recorded in [progress](progress.md). Manual
GUI, editor/browser, and two-physical-computer checks remain pending.
