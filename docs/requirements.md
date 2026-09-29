# Assignment requirements and evidence plan

Source: the supplied [*Term Project: Socket Programming* brief](assignment/socket-programming-brief.pdf)
(pages 1-2) and [*Implementation Explanation* slide template](assignment/implementation-explanation-template.pdf)
(pages 1-20). The assignment
allows any programming language or platform, but requires separate client and
server program files. The current repository is at **Stage 1** (schema,
framing, and tests). **Every end-to-end requirement below is pending**; the
schema and framing tests alone do not satisfy any graded chat behavior.

The score is 12.5 points. Part 1 must be complete before Part 2 is graded;
Part 2 must be complete before Part 3 is graded. A live demonstration and
implementation-explanation slides are required.

| Requirement | Mandatory assignment criterion | Planned implementation | Eventual evidence | Stage 1 status |
| --- | --- | --- | --- | --- |
| R1 (1.0) | Present the architecture with at least two clients on different physical computers; identify each network protocol, why it was chosen, and how it is used. | Separate server and client programs from Stage 2; architecture and protocol documentation. | Run clients on two physical computers over the network and present an architecture diagram and protocol explanation. | Pending |
| R2 (0.5) | Server-client chat messages use socket programming only. | TCP transport from Stage 2; chat message routing from Stages 5-6. | Capture the socket send/receive code and show a message crossing the network. | Pending |
| R3 (0.5) | Each client has a unique name. | Server-side login and duplicate-name rejection in Stage 4. | Connect two clients with the same name; second login is rejected. | Pending |
| R4 (0.5) | Each client sees all currently connected names, including its own. | Server presence snapshots in Stage 7; client/GUI list. | Join and disconnect clients; verify every connected client sees the updated list and itself. | Pending |
| R5 (0.5) | Every private and group chat has its own room. | Direct rooms in Stage 5, group rooms in Stage 6, later GUI room selection. | Open two conversations and show their independent views and histories. | Pending |
| R6 (0.5) | Each room has a text input/send control and a window for sent and incoming messages. | Later GUI using the reusable client API. | Send and receive messages in both a private room and a group room. | Pending |
| R7 (1.0) | A user can DM anyone in the connected-user list; only sender and recipient see it. | Server-side direct routing and authorization in Stage 5. | With Alice, Bob, and Charlie connected, DM Bob from Alice; Charlie receives nothing. | Pending |
| R8 (1.0) | Each user can create groups; initially only the creator is a member. | Server group creation in Stage 6. | Alice creates a group; its member list contains only Alice. | Pending |
| R9 (1.0) | Every user sees all groups and each group's members. | Group discovery in Stage 6 and update snapshots in Stage 7; later GUI list. | Show the same group/member list to creator, member, and nonmember clients. | Pending |
| R10 (1.0) | A user joins a group only through their own action; the creator does not add them automatically. | Self-join request and server-side membership checks in Stage 6. | Bob explicitly joins Alice's group; Charlie stays out. | Pending |
| R11 (1.0) | A joined user can message the group; only group members see it. | Server-side group routing and authorization in Stage 6. | Alice and Bob receive a group message; nonmember Charlie receives nothing. | Pending |

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
All four features are **pending at Stage 1**.

## Chosen design, not assignment mandates

The team's selected direction is C++20, CMake/vcpkg, Boost.Asio TCP sockets,
Protocol Buffers with bounded four-byte length framing, and GoogleTest.
SQLite/WAL, heartbeats, reconnect support, and TLS 1.3 are later design goals.
These tools and features are engineering choices from the prior project
discussion, **not required by the assignment**. The assignment's required
behavior and two-computer demonstration remain the acceptance criteria.
