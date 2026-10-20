"""Create an isolated development CA and DNS/IP SAN server certificate; never overwrite credentials."""

import argparse
import ipaddress
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(openssl: str, arguments: list[str], folder: Path) -> None:
    """Run OpenSSL without shell interpolation or private-key output, and fail on errors."""
    result = subprocess.run([openssl, *arguments], cwd=folder, capture_output=True, text=True, check=False)
    if result.returncode:
        raise RuntimeError(f"OpenSSL {arguments[0]} failed: {result.stderr.strip()}")


def generate(openssl: str, folder: Path, dns: list[str], ips: list[str]) -> None:
    """Issue a 30-day leaf from a new one-year CA in a previously empty directory."""
    if any(not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9.-]{0,252}", name) for name in dns):
        raise ValueError("DNS names must contain only ASCII letters, digits, dots, and hyphens")
    addresses = [str(ipaddress.ip_address(value)) for value in ips]
    if not dns and not addresses:
        raise ValueError("at least one DNS or IP SAN is required")
    folder = folder.resolve()
    if folder.exists() and any(folder.iterdir()):
        raise ValueError("output directory must be empty; existing credentials are never overwritten")
    folder.mkdir(parents=True, exist_ok=True)
    if os.name != "nt":
        folder.chmod(0o700)
    (folder / "ca.cnf").write_text(
        "[req]\ndistinguished_name=dn\nx509_extensions=ca_ext\nprompt=no\n"
        "[dn]\nCN=Socket Chat Development CA\n[ca_ext]\n"
        "basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\n"
        "subjectKeyIdentifier=hash\n", encoding="ascii")
    san = ",".join([f"DNS:{name}" for name in dict.fromkeys(dns)] +
                   [f"IP:{value}" for value in dict.fromkeys(addresses)])
    (folder / "server.cnf").write_text(
        "[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=Socket Chat Development Server\n"
        "[server_ext]\nbasicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\n"
        f"extendedKeyUsage=serverAuth\nsubjectAltName={san}\n"
        "subjectKeyIdentifier=hash\nauthorityKeyIdentifier=keyid,issuer\n", encoding="ascii")
    run(openssl, ["req", "-new", "-x509", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                  "-nodes", "-keyout", "ca.key", "-out", "ca.pem", "-days", "365", "-sha256",
                  "-config", "ca.cnf"], folder)
    run(openssl, ["req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
                  "-nodes", "-keyout", "server.key", "-out", "server.csr", "-sha256",
                  "-config", "server.cnf"], folder)
    run(openssl, ["x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key",
                  "-set_serial", "0x" + secrets.token_hex(16), "-out", "server.pem", "-days", "30",
                  "-sha256", "-extfile", "server.cnf", "-extensions", "server_ext"], folder)
    run(openssl, ["verify", "-CAfile", "ca.pem", "-purpose", "sslserver", "server.pem"], folder)
    if os.name != "nt":
        for name in ("ca.key", "server.key"):
            (folder / name).chmod(0o600)
    print(f"Development certificates: {folder}")
    print("Distribute only ca.pem to clients. Keep ca.key private/offline and server.key on the server.")


def main() -> int:
    """Validate SAN arguments and locate the separate OpenSSL command-line executable."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "certs/development")
    parser.add_argument("--openssl", default=os.environ.get("OPENSSL_EXECUTABLE", "openssl"))
    parser.add_argument("--dns", action="append", help="Additional DNS SAN; localhost is always included")
    parser.add_argument("--ip", action="append", help="Additional IP SAN; 127.0.0.1 and ::1 are always included")
    args = parser.parse_args()
    executable = shutil.which(args.openssl)
    if not executable:
        parser.error("OpenSSL CLI is missing; add it to PATH or pass --openssl <executable>")
    try:
        generate(executable, args.output, ["localhost", *(args.dns or [])], ["127.0.0.1", "::1", *(args.ip or [])])
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"Development TLS: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
