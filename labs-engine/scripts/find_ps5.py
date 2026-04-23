"""
find_ps5.py — UDP-broadcast discovery for PS4 / PS5 on your local network.

Sends Sony's "SRCH * HTTP/1.1" probe to port 9302, listens for any console
responding, prints what it finds.

Run:
    python find_ps5.py
"""
import socket
import sys
import time

PROBE_PS4 = b"SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00010010\n"
PROBE_PS5 = b"SRCH * HTTP/1.1\ndevice-discovery-protocol-version:00030010\n"
PORT      = 9302
TIMEOUT   = 2.5


def parse_response(data: bytes) -> dict:
    out = {}
    text = data.decode("utf-8", errors="ignore")
    for line in text.splitlines():
        if ":" in line:
            k, _, v = line.partition(":")
            out[k.strip().lower()] = v.strip()
    return out


def discover() -> list[dict]:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.settimeout(TIMEOUT)
    try:
        sock.bind(("", 0))
    except Exception as ex:
        print(f"bind failed: {ex}")
        return []

    # Probe both protocols on broadcast
    sock.sendto(PROBE_PS5, ("255.255.255.255", PORT))
    sock.sendto(PROBE_PS4, ("255.255.255.255", PORT))

    found = {}
    end = time.perf_counter() + TIMEOUT
    while time.perf_counter() < end:
        try:
            data, addr = sock.recvfrom(2048)
        except socket.timeout:
            break
        meta = parse_response(data)
        meta["ip"] = addr[0]
        # de-dupe by IP — first response wins
        found.setdefault(addr[0], meta)

    sock.close()
    return list(found.values())


def main() -> int:
    print(f"Broadcasting to UDP {PORT} on 255.255.255.255 (timeout {TIMEOUT}s)...")
    consoles = discover()
    if not consoles:
        print("No PS4/PS5 found.")
        print("  • Make sure the console is on (or in Rest Mode with 'Stay Connected to Internet' on)")
        print("  • PC and console must be on the same network")
        print("  • Some routers block client-to-client broadcast — try wired Ethernet")
        return 1
    print(f"\nFound {len(consoles)} console(s):")
    for c in consoles:
        ip   = c.get("ip", "?")
        name = c.get("host-name", c.get("host-id", "(unknown)"))
        typ  = c.get("host-type", c.get("system-version", "?"))
        st   = c.get("status_code", c.get("running-app-name", ""))
        print(f"  {ip:<16}  {typ:<8}  {name}  {st}")
    print()
    print("Copy the IP and paste into LabsEngine settings as `ps/host`.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
