# TLS 1.3 setup and transport

Stage 12 encrypts the framed chat protocol with TLS 1.3. Server, CLI, and Windows
GUI executables require an explicit transport mode. TLS clients verify the server
chain, validity, and expected DNS/IP Subject Alternative Name (SAN) before login
or chat. They never fall back to plaintext after verification or negotiation failure.

## Generate development certificates

The OpenSSL command-line executable must be on `PATH`, separately from the
libraries linked by vcpkg. It is available in the current Windows environment.
If necessary, pass `--openssl C:/path/to/openssl.exe` or set `OPENSSL_EXECUTABLE`.
No editor include-path changes are needed: CMake exports the OpenSSL dependency.

```powershell
python tools/dev_tls.py
```

This creates a new development CA and 30-day leaf certificate under ignored
`certs/development/`, with `localhost`, `127.0.0.1`, and `::1` SANs. The CA lasts
one year. The output directory must be empty; existing credentials are never
overwritten. For LAN names/IPs, choose a new directory and actual server values:

```powershell
python tools/dev_tls.py --output certs/development/lan --dns chat.example --ip 192.168.1.50
```

An IP connection needs an IP SAN, not a DNS SAN containing IP text. Changing a
bind address does not change its certificate. When generating a new CA,
distribute its new public certificate to clients through a trusted channel.

| File | Use |
| --- | --- |
| `ca.pem` | Public trust root; distribute this file to clients. |
| `ca.key` | Private CA signing key; keep private/offline, never distribute to clients. |
| `server.pem` | Public leaf certificate; append intermediate certificates after it for a larger PKI. |
| `server.key` | Matching unencrypted key; keep on the server with access limited to its operator. |
| `.cnf` / `.csr` | Local issuance configuration and certificate request. |

The directory is ignored by Git. Keys are unencrypted for unattended startup;
file access controls protect them. The generator limits POSIX directory/key
permissions; Windows uses the directory's inherited ACL. The server never
prompts for a key password. Do not commit development credentials.

## Run secure server and clients

Build Debug, then run separate programs from the repository root:

```powershell
build/debug/chat_server.exe --tls certs/development/server.pem certs/development/server.key 127.0.0.1 5555
build/debug/chat_gui.exe --tls certs/development/ca.pem
build/debug/chat_cli.exe --tls certs/development/ca.pem 127.0.0.1 5555 42 Alice_01
```

The GUI Host field is also its expected certificate SAN by default. To connect
by address while authenticating a specific DNS service:

```powershell
build/debug/chat_gui.exe --tls certs/development/ca.pem --server-name localhost
build/debug/chat_cli.exe --tls certs/development/ca.pem --server-name localhost 127.0.0.1 5555 42 Alice_01
```

`--server-name` fixes the expected SAN and DNS SNI independently of the connect
address. It never disables verification. Reconnect retains the same trust/name
settings and revalidates each handshake. An invalid replacement ends recovery.

```text
chat_server --tls <certificate-chain.pem> <server.key> [bind-address] [port] [database-path] [idle-timeout-ms]
```

Port zero prints the selected port. Database/idle semantics are unchanged.
Credentials/key matching are checked before listening. For LAN use, bind the
actual LAN address or `0.0.0.0`, distribute only `ca.pem`, and connect using a SAN
present in the certificate. Two-physical-computer acceptance remains pending.

## Explicit loopback development mode

```powershell
build/debug/chat_server.exe --plaintext-dev 127.0.0.1 5555
build/debug/chat_cli.exe --plaintext-dev 127.0.0.1 5555 42 Alice_01
build/debug/chat_gui.exe --plaintext-dev
```

The flag is required. Plaintext listeners bind only loopback; client resolution
must produce only loopback endpoints. Wildcard/LAN plaintext listeners and remote
plaintext connections are rejected. TLS listeners never auto-detect plaintext;
TLS 1.2 peers cannot negotiate. Mixed flags and old invocations without a mode fail.

Embedded `PingServer`, `ChatClient`, `ChatWorker`, and `Window` callers retain
disabled-TLS defaults for development/tests, with the same loopback restriction.
For network use, pass GUI-independent settings:

```cpp
chat::transport::ClientTlsOptions trust{true, "ca.pem", "chat.example"};
chat::client::ChatClient client(trust);
client.connect("192.168.1.50", 5555);
chat::client::ChatWorker worker({}, trust);
chat::transport::ServerTlsOptions credentials{true, "server.pem", "server.key"};
chat::server::PingServer server(context, endpoint, "chat.sqlite3", {}, credentials);
```

## Ownership and failure behavior

TLS retains the existing socket owner, ordered bounded writer, and framing.
Server SSL operations share the hub strand; application I/O begins after
handshake. Context and active-buffer ownership extend through pending handlers.
Protobuf tags, retry keys, history, and server authorization are unchanged.

Client connect's three-second deadline covers DNS, TCP, and TLS. Server handshake
has an independent three-second deadline (configurable 1..60,000 ms), even with
idle expiry disabled. Prelogin handshakes consume the existing 64-connection cap.
Handshake bytes do not refresh application idle activity. After handshake,
existing heartbeat/idle behavior applies to decrypted complete requests.

Certificate/configuration/SSL protocol failures throw terminal `TlsError`.
Transport loss stays `NetworkError`, eligible for bounded durable recovery. Soft
polls retain partial decrypted frames. Stop closes the lowest socket and cancels
TLS/timer work; callbacks retain buffers through cancellation. Close is abortive,
without a concurrent graceful-shutdown writer or a wait for `close_notify`.
Unexpected TLS EOF is transport failure, never a successful incomplete frame.

Only supplied CA roots are trusted. Native OpenSSL checks chain validity and
server purpose. DNS/IP SAN is required; Common Name fallback and partial-label
wildcards are disabled. This stage authenticates the server, without client
certificates or real-world identity checks. It does not encrypt SQLite or add
revocation/rotation services. Login/resume capabilities and server authorization
still govern participants and room privacy. TLS early data is not enabled.
Version and identity setup use the
[OpenSSL version](https://docs.openssl.org/3.6/man3/SSL_CTX_set_min_proto_version/)
and [peer verification](https://docs.openssl.org/3.6/man3/SSL_set1_host/) APIs;
I/O uses the [Boost.Asio SSL stream](https://www.boost.org/latest/doc/html/boost_asio/reference/ssl__stream.html).

## Automated verification

```powershell
python tools/check_tls_apps.py
```

The smoke checks run server/CLI processes without visible windows, verify
independent Python TLS 1.3 negotiation, login/Ping, wrong-name rejection,
explicit modes, missing credentials, and loopback development. C++ tests
generate temporary CAs/keys under ignored build output and remove them after
each test. They cover SAN/trust/validity/signature/key failures, TLS 1.2/plaintext
denial, encrypted split/coalesced/max-size frames, three-client privacy, stored
retry/sync, handshake deadlines/cancellation, and encrypted worker recovery.
Hidden controls verify secure login/send and responsive certificate-failure status.
See [progress](progress.md) for results. Visible GUI/editor/browser and physical
network checks remain pending.
