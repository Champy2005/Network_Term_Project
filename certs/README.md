# Certificates

Stage 12 implements TLS 1.3 with explicit CA trust and DNS/IP SAN verification.
Run `python tools/dev_tls.py` from the repository root to create a development CA
and server certificate in ignored `certs/development/`. The OpenSSL command-line
executable must be on `PATH`; use `--openssl` for another path. Existing
credentials are never overwritten.

Distribute only `ca.pem` to clients. Keep `ca.key` private/offline and `server.key`
on the server. Do not commit keys, passwords, or deployment certificates. For LAN
names/IPs, expiry, runnable commands, trust configuration, and tests, follow the
[TLS setup guide](../docs/tls.md).
