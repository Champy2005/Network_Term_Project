# Message receipts and retries

Stage 10 assigns server message IDs, per-room sequences, and timestamps to every
accepted direct/group message. The headless API exposes them in operation
results, deliveries, and history. The GUI still shows server echoes and keeps
local transcripts. [Stage 11 recovery](recovery.md) now retains uncertain message
attempts and replays missing sequences during automatic identity resume.

## Three different identifiers

| Identifier | Owner and purpose |
| --- | --- |
| `Envelope.request_id` | Client-generated exchange correlation; the response echoes it. Reusing it alone does not deduplicate messages. |
| `client_message_id` | Optional client retry key, scoped to the sender principal across direct and group messages. Save it before attempting a send. |
| `message_id` / `room_sequence` | Server-issued receipt. Every accepted message has a stable ID and a positive sequence in its own room. |

Sequence numbers start at one and increase by one for new messages in a room,
including messages from different senders. Other rooms have independent counters.
An accepted duplicate does not consume a sequence. Timestamps are wall-clock
milliseconds; they may move backward if the system clock changes. Sequences
establish order. History's global integer cursor remains separate from these
per-room counters.

## Sending and explicit retry

`send_direct` and `send_group` generate a fresh random 256-bit retry key for each
call. `OperationResult` includes the key, server message ID, room ID, and sequence
on success. A new call without a saved key represents a new message, even if its
text is identical.

For an attempt whose result may be lost, create and retain a key first, then use
the explicit methods:

```cpp
const auto key = chat::client::ChatClient::new_message_key();
// Save the key, exact target/text, and original login resume token before sending.
const auto result = alice.send_direct_with_key("bob", "hello", key);
// After an uncertain failure: reconnect, resume the original identity, and use
// the same target/text/key. Never substitute a fresh name-only login for resume.
const auto retry = alice.send_direct_with_key("bob", "hello", key);
// Group equivalent: send_group_with_key(group_id, text, key), after explicit rejoin.
```

Keys contain 1-64 ASCII letters, digits, underscores, or hyphens. Invalid explicit
keys throw before network work; the server also validates keys independently.
Empty wire keys preserve legacy requests: each send is a distinct message and
has no retry guarantee. Keys are omitted from deliveries and history. They are
identifiers rather than authentication secrets; the login capability proves
which sender owns a stored key.

The server binds a key to the accepted message's type, target, and exact text,
including embedded NUL bytes. An exact retry returns the original receipt under
the new request's correlation ID. It creates no row, echo, or recipient fanout.
Changed content, target, or message type returns `message_id_conflict`. Rejected
requests and rolled-back transactions do not reserve a key. Another sender can
use the same key without accessing or colliding with the original sender's row.
Group create/join and history operations do not have message retry semantics.

## Authorization on retries

Direct retries are looked up by the authenticated durable sender. The recipient
may now be offline: an already accepted receipt can still be recovered. A new
message still requires a connected recipient. If somebody else claims the old
recipient's name, the accepted retry remains bound to its original room and
does not deliver to that replacement. Use a fresh key for a newly intended
message; the current recipient's identity then determines its room.

Every group attempt requires current membership, including accepted retries.
After disconnect/restart, resume the identity and explicitly self-join before
retrying. Stored admissions do not grant live membership. Rejected/unregistered
clients cannot query another sender's receipts or bypass routing authorization.

## Persistence and bounds

Storage-enabled servers keep deduplication for the lifetime of each stored
message. In one SQLite transaction they create/find the room, insert the
message/key/metadata, and advance the room counter. Only a successful commit
allows an ACK or delivery. Unique indexes on `(principal,client_key)` and
`(room,sequence)` protect identities and order. Failed inserts, counter updates,
or commits roll back together. The store checks a key again within the write
transaction. Stored keys/messages grow with the database; retention is not yet
implemented.

Schema v2 automatically upgrades v1 in a transaction before the listener opens.
Existing cursor IDs, text, participant pairs, and admissions are preserved.
Messages get `message-<database-row-id>` IDs and sequences in original per-room
cursor order. Legacy timestamps are zero because the old schema had no clock
data; new timestamps are populated. Room counters continue after the migrated
records. Failed migration rolls back schema changes and version advancement.
Stop the server and back up the database as described in [history](history.md)
before operating on an existing store. Schema v2 cannot be opened by old code
that only supports v1. Migration uses SQLite's
[window functions](https://www.sqlite.org/windowfunctions.html) for room ordering.

Durable server IDs belong to one database; treat IDs as opaque and do not merge
independent database namespaces. Ephemeral servers use a random run namespace
and an increasing message counter. Their sequences last as long as the room
state, and each connection retains its most recent 256 accepted keyed messages
in FIFO order, with bounded key/target/text/receipt strings. Duplicate attempts
do not refresh that window. Legacy empty-key sends do not consume cache slots.
After eviction, disconnect, or restart, an ephemeral retry can become a new
message. Use a storage-enabled server for durable retry protection.

## Limits of the receipt

An ACK proves acceptance (and commit when storage is enabled), not recipient
receipt or display. Routing happens after commit and can fail because a peer
disconnects or overflows its queue. A duplicate never repeats that fanout, even
when a recipient missed the original delivery. Explicit authorized history can
recover stored text; the worker also performs automatic sequence catch-up. Exactly-once delivery is not
promised.

The client validates correlation, message metadata bounds, positive sequences,
group ACK targets, server-only delivery fields, and increasing history sequences.
Malformed responses close the connection. New clients require these metadata
fields on message ACKs/deliveries; an older server that omits them is unsupported.
Older clients can still send empty-key v1 requests to this server. Framing and
all existing Protobuf tag numbers are unchanged.

The worker counts key/ID strings in its bounded queues and owns every networking
call through one ordered writer. During recoverable transport loss it retains
pending keys and retries messages after identity resume, chosen-group rejoin, and
sequence replay. Already applied sequences are suppressed. Terminal failure or
manual disconnect discards pending attempts; a later manual Send uses a fresh key
and can duplicate uncertain earlier acceptance. Recovery is limited to the running
worker, as described in [recovery](recovery.md).

Eighteen automated cases cover matching receipts/history, direct/group ordering,
concurrent senders, duplicates/conflicts/privacy, restart and identity replacement,
an unread ACK, failed direct/group commits, legacy requests, bounded ephemeral
eviction, key validation, successful/failed migration, and malformed metadata.
The existing history response test also checks missing/repeated sequence fields.
See [progress](progress.md) for actual build results. Manual GUI, editor/browser,
and physical-network acceptance remain pending.
