# Assignment requirements and evidence plan

Source: the supplied [*Term Project: Socket Programming* brief](assignment/socket-programming-brief.pdf)
(pages 1-2) and [*Implementation Explanation* slide template](assignment/implementation-explanation-template.pdf)
(pages 1-20). The assignment
allows any programming language or platform, but requires separate client and
server program files. The current repository implements **Stages 0-13**:
framing, TCP, bounded sessions, unique active usernames, private direct rooms,
groups with voluntary joining, and presence/group snapshots. Server and
headless API tests establish the underlying behavior for R2-R5 and R7-R11.
The Stage 14 Windows room controls/views now have model, worker, and hidden
native-control tests. Manual GUI acceptance, live demonstrations, required code
snapshots, and the physical-computer demonstration remain pending; automated checks alone do
not complete the graded acceptance.

The score is 12.5 points. Part 1 must be complete before Part 2 is graded;
Part 2 must be complete before Part 3 is graded. A live demonstration and
implementation-explanation slides are required.

| Requirement | Mandatory assignment criterion | Planned implementation | Eventual evidence | Current status |
| --- | --- | --- | --- | --- |
| R1 (1.0) | Present the architecture with at least two clients on different physical computers; identify each network protocol, why it was chosen, and how it is used. | Separate server and client programs from Stage 2; architecture and protocol documentation. | Run clients on two physical computers over the network and present an architecture diagram and protocol explanation. | Pending |
| R2 (0.5) | Server-client chat messages use socket programming only. | TCP transport from Stage 2; chat message routing from Stages 5-6. | Capture the socket send/receive code and show a message crossing the network. | Automated socket chat passes; live demonstration/code snapshot pending |
| R3 (0.5) | Each client has a unique name. | Server-side login and duplicate-name rejection in Stage 4. | Connect two clients with the same name; second login is rejected. | Automated server checks pass; live demonstration/code snapshot pending |
| R4 (0.5) | Each client sees all currently connected names, including its own. | Server presence snapshots in Stage 7; client/GUI list. | Join and disconnect clients; verify every connected client sees the updated list and itself. | Automated server/API/Windows list checks pass; manual GUI/live demonstration/code snapshot pending |
| R5 (0.5) | Every private and group chat has its own room. | Direct rooms in Stage 5, group rooms in Stage 6, Windows GUI room selection. | Open two conversations and show their independent views and histories. | Automated room identity and Windows view separation pass; manual GUI/live demonstration/code snapshot pending |
| R6 (0.5) | Each room has a text input/send control and a window for sent and incoming messages. | Windows GUI using the reusable client API and worker/model. | Send and receive messages in both a private room and a group room. | Automated Windows controls pass; manual GUI/live demonstration/code snapshot pending |
| R7 (1.0) | A user can DM anyone in the connected-user list; only sender and recipient see it. | Server-side direct routing and authorization in Stage 5. | With Alice, Bob, and Charlie connected, DM Bob from Alice; Charlie receives nothing. | Automated three-client privacy and Windows send controls pass; manual GUI/live demonstration/code snapshot pending |
| R8 (1.0) | Each user can create groups; initially only the creator is a member. | Server group creation in Stage 6. | Alice creates a group; its member list contains only Alice. | Automated server/API/Windows creation passes; manual GUI/live demonstration/code snapshot pending |
| R9 (1.0) | Every user sees all groups and each group's members. | Group discovery in Stage 6 and update snapshots in Stage 7; Windows GUI list. | Show the same group/member list to creator, member, and nonmember clients. | Automated server/API/Windows discovery passes; manual GUI/live demonstration/code snapshot pending |
| R10 (1.0) | A user joins a group only through their own action; the creator does not add them automatically. | Self-join request and server-side membership checks in Stage 6. | Bob explicitly joins Alice's group; Charlie stays out. | Automated self-join/authorization and Windows join controls pass; manual GUI/live demonstration/code snapshot pending |
| R11 (1.0) | A joined user can message the group; only group members see it. | Server-side group routing and authorization in Stage 6. | Alice and Bob receive a group message; nonmember Charlie receives nothing. | Automated member-only delivery and Windows room controls pass; manual GUI/live demonstration/code snapshot pending |

The slide template asks for an architecture diagram and protocol explanation
for R1, a code snapshot for R2, and **mandatory code snapshots for R3-R11**.
It also provides places for generative AI prompts if used.

## Special features (Part 3: four points)

Four features can earn one point each; **at least one must integrate AI**
(the brief gives speech-to-text, summarization, sentiment analysis, and chatbot
as examples). The first feature is therefore reserved for an AI feature; its
specific implementation and model are not yet selected. The other three
features are to be selected after the fundamentals work. Optional cloud
deployment may count as one special point. Each feature needs a live demo and
a mandatory code snapshot; the AI slide also requests the model and reference.
All four features are **pending**.

## Chosen design, not assignment mandates

The team's selected direction is C++20, CMake/vcpkg, Boost.Asio TCP sockets,
Protocol Buffers with bounded four-byte length framing, and GoogleTest.
SQLite/WAL history, heartbeat cleanup, and message receipts/retry deduplication
and automatic worker reconnect/sync and verified TLS 1.3 are implemented.
These tools and features are engineering choices from the prior project
discussion, **not required by the assignment**. The assignment's required
behavior and two-computer demonstration remain the acceptance criteria.
