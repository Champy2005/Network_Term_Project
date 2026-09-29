# Certificates

Stage 1 does not use TLS or load files from this directory. TLS 1.3 is planned
after the plaintext TCP client and server work.

When TLS is implemented, keep the server private key out of version control.
The client must verify the server certificate chain and the hostname it
connected to; accepting every certificate would defeat the purpose of TLS.
For a local demonstration, create a development CA, issue a certificate whose
Subject Alternative Name matches the server's LAN hostname or IP address, and
distribute only the CA certificate to clients. Document the generation and
configuration commands alongside the TLS implementation.

Do not commit private keys, passwords, or real deployment certificates here.
