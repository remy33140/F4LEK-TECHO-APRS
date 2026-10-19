#!/usr/bin/env python3
"""Fake APRS-IS server for testing the app's Web mode without touching the real network.

    python3 tools/fake_aprsis.py --port 14581 [--from F4LEK-7]

In the Android emulator the PC is 10.0.2.2: set the Web server to 10.0.2.2:14581.
It answers the login ("verified" when the passcode matches the callsign), sends one message
to the logged-in callsign twice (a resend, as a station does while waiting for its ack),
acks every message the app sends, sends a "#" keepalive every 20 s, and prints every line.
"""
import argparse
import re
import socket
import threading
import time


def passcode(call):
    call = call.upper().split("-")[0]
    h = 0x73E2
    for i in range(0, len(call), 2):
        h ^= ord(call[i]) << 8
        if i + 1 < len(call):
            h ^= ord(call[i + 1])
    return h & 0x7FFF


def log(direction, line):
    print(f"{time.strftime('%H:%M:%S')} {direction} {line}", flush=True)


def handle(conn, peer_call, no_ack=False):
    def send(line):
        log(">>", line)
        conn.sendall((line + "\r\n").encode("latin-1"))

    send("# aprsc 2.1.19 fake-for-tests")
    f = conn.makefile("r", encoding="latin-1", newline="\r\n")
    login = f.readline().strip()
    log("<<", login)
    m = re.match(r"user (\S+) pass (-?\d+)", login)
    if not m:
        return
    call, pw = m.group(1).upper(), int(m.group(2))
    ok = pw == passcode(call)
    send(f"# logresp {call} {'verified' if ok else 'unverified'}, server FAKE-IS")

    def script():
        time.sleep(3)
        msg = f"{peer_call}>APL2OY,WIDE1-1,qAR,F5XYZ-10::{call:<9}:Bien arrive au sommet, 73{{021"
        send(msg)
        time.sleep(4)
        send(msg)  # the same message again: the app must ack it again but not show it twice
        while True:
            time.sleep(20)
            send("# keepalive")

    threading.Thread(target=script, daemon=True).start()
    for line in f:
        line = line.strip()
        if not line:
            continue
        log("<<", line)
        mm = re.match(r"(\S+?)>[^:]*::(\S+)\s*:(.*)\{(\w+)$", line)
        if mm and not mm.group(3).startswith("ack") and not no_ack:
            to, mid = mm.group(2), mm.group(4)

            def ack(to=to, mid=mid):
                time.sleep(2)
                send(f"{to}>APL2OY,WIDE1-1,qAR,F5XYZ-10::{call:<9}:ack{mid}")

            threading.Thread(target=ack, daemon=True).start()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=14581)
    ap.add_argument("--from", dest="peer", default="F4LEK-7", help="callsign that writes to the app")
    ap.add_argument("--no-ack", action="store_true", help="never ack: to watch the app's resends")
    a = ap.parse_args()
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", a.port))
    srv.listen(4)
    log("--", f"fake APRS-IS on port {a.port}")
    while True:
        conn, addr = srv.accept()
        log("--", f"client {addr[0]}")
        threading.Thread(target=handle, args=(conn, a.peer, a.no_ack), daemon=True).start()


if __name__ == "__main__":
    main()
