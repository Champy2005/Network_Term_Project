# Reconnect and sequence catch-up

Stage 11 adds automatic recovery to the GUI-independent `ChatWorker`. With a
storage-enabled server, a transport failure resumes the same durable participant,
self-rejoins previously chosen groups, discovers private rooms, and replays missing
messages before reporting **Reconnected**. Drafts, selected rooms, and transcripts
remain in the window. The blocking `ChatClient` exposes the same primitives for
headless callers, which schedule their own recovery.

## Identity and delivery policy

The worker retains the login resume token only in memory for its current run.
Recovery always uses `resume_login`; a rejected capability ends the worker. It
never falls back to claiming the display name as a fresh identity. A manual
Disconnect followed by Connect starts a fresh login and clears the old window
state. Closing the application does not save tokens, room cursors, drafts, or
pending sends. Embedded ephemeral servers issue no capability and do not recover
automatically. The current client and server versions must be used together.

Group membership still belongs to the active server connection. The worker
remembers successful create/self-join choices and self-rejoins those groups on
resume. An uncertain join also retains the user's join intent. It never uses
historical admission rows to authorize membership and never joins merely discovered
groups. Current members may read the full retained group history, as defined in
[history](history.md). A new group view starts at cursor zero; replay can therefore
include earlier retained text when the next live gap or reconnect is processed.
This change does not add a general history browser or immediate history loading
when a quiet group is first joined.

Direct discovery is scoped to the authenticated durable principal. It includes
rooms with offline original peers, including a room whose first ACK/delivery was
lost. Each room reports `peer_connected` only if the original durable peer is
currently connected. The GUI can reactivate that room and retain its draft;
presence of a fresh user with the same name alone cannot reactivate it. A new
direct send still resolves the currently connected target on the server.

## Ordered replay

`SyncRequest` (Envelope tag 32) carries a room ID, `after_sequence`, page limit,
and `through_sequence`. Limit zero means 50; the maximum is 100. Through zero
captures the server's current committed room watermark. `SyncResponse` (tag 33)
returns contiguous entries, the last returned sequence, the captured watermark,
and `has_more`. Pass the watermark unchanged on subsequent pages, so new live
writes cannot move the end of that replay. Sequence cursors differ from the
global database cursors used by `history()`.

```cpp
std::uint64_t cursor = 0; // Load this identity/room's last applied sequence.
std::uint64_t watermark = 0;
bool more = true;
while (more) {
    const auto page = client.sync(room_id, cursor, watermark, 50);
    if (!page.accepted) {
        throw std::runtime_error(page.reason);
    }
    watermark = page.through_sequence;
    for (const auto& record : page.records) {
        // Apply the record before advancing its room cursor.
        cursor = record.room_sequence;
    }
    more = page.has_more;
}
```

`DirectRoomsRequest`/`DirectRoomsResponse` use tags 34/35. They enumerate only
the caller's direct rooms, with an ascending room-ID cursor and at most 64 rooms
per page. Discovery's `last_sequence` is informational; it must not advance an
applied-message cursor. No existing Protobuf field numbers, protocol version,
four-byte big-endian framing, or schema v2 fields changed.

The worker records a room cursor only after admitting its delivery into the
bounded UI update queue. It suppresses live/replayed sequences already admitted
and fills a live sequence gap before publishing the later delivery. Login/join
snapshots reach the model before group replay. The model also suppresses already
applied positive sequences and retains its cursor after transcript eviction.
Each room is ordered independently; no cross-room ordering is promised.

Unauthorized rooms return `history_forbidden`. Missing sequences, a cursor ahead
of the committed watermark, or an impossible replay range return `sync_gap`
(error code 11). These failures end worker recovery explicitly instead of skipping
text or resetting cursors. Malformed pages also close the headless connection.
No retention/deletion policy exists yet; deleting stored messages can cause a gap.

## Uncertain sends and bounded retries

Every queued direct/group send gets a random retry key before its first attempt.
An uncertain active message and the remaining command queue survive a transport
loss. After resume/rejoin/replay, the worker retries the exact target, text, and
key. A previously committed message returns its original ACK without another
insert, echo, or fanout. Replay supplies the missed echo. Only one completion is
published for the submitted command. See [message reliability](reliability.md)
for the server's durable deduplication policy.

Group creation lacks a retry key. A lost response reports **operation outcome
unknown** and does not blindly create another group. An uncertain self-join is
reported the same way, while the user's retained join intent is restored during
recovery. Typed server rejections complete normally; malformed responses,
authorization failures, and queue overflow are terminal rather than retryable.
`NetworkError` identifies transport EOF/reset/refusal/operation timeout for
headless callers; other protocol failures remain `std::runtime_error`.

| Worker setting | Default and bound |
| --- | --- |
| Reconnect attempts | 16 consecutive attempts; 0 disables recovery; maximum 64 |
| Retry delay | 250 ms, doubled up to 2,500 ms; positive, maximum cap 60 seconds |
| Recovery room cursors | 256; configurable from 1 to 256; exceeding the bound ends explicitly |
| Commands | 64 queued plus one active; retained until completion or terminal shutdown |
| UI updates | 256 / 2 MiB, including replay records and receipt strings |
| Operation deadline | 3 seconds for each connect, login, join, page, send, or Ping |

Backoff is stop-aware; Disconnect wakes it immediately. An active network
operation remains bounded by its deadline, and socket cancellation/ownership
stays on the worker thread. A successful complete recovery resets the retry
budget. An old session still occupying the name is retried within that budget;
other resume denials are terminal. Server idle expiry remains 30 seconds by
default. Custom expiry and retry budgets must be chosen together.

Recovery uses the existing bounded event/write paths. If a GUI does not drain
updates, or live traffic overwhelms the client during paging, overflow ends the
worker explicitly. Local displays retain up to 100 lines per room and 2 MiB of
sender/text/receipt strings; older displayed lines can be evicted even though
every replay record was applied. Durable text remains on the server. These
semantics do not promise exactly-once delivery to every recipient or recovery
across a client process restart.

## Verification

Automated socket tests interrupt a real TCP proxy after commit but before ACK,
pause connections during 120 missed group messages, skip a live sequence, and
restart a file-backed server. They verify original identity, one completion and
one displayed echo, contiguous multi-page replay, stable watermarks, chosen-group
rejoin, nonmember/private-room denial, cancellation, retry/room bounds, invalid
capability rejection, uncertain group creation, and malformed responses. Model
tests retain drafts/cursors and suppress duplicates after eviction. A hidden
Windows control test verifies the selected group, draft, transcript, status, and
send/disconnect availability across recovery. See [progress](progress.md) for
the actual preset results. Visible GUI, editor/browser, and physical-network
acceptance remain pending.
