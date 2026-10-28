#!/usr/bin/env python3
"""A stand-in for Kosmos, for developing a native addon without starting the game.

It listens on 127.0.0.1:9420, grants everything, answers get/set from a small property table, pushes subscribed
values, and prints the keys, MFD pages and spawns a program asks for. Run it, then start your program with
KOSMOS_LINK=127.0.0.1:9420 in the environment.
"""
import math
import socket
import struct
import sys
import threading
import time

sys.path.insert(0, __file__.rsplit("/", 2)[0] + "/examples")
from kosmos_link import (DEFINE_KEY, DEFINE_MFD, DRAW, ERROR, EVENT, GET, GIVE, HELLO, LOG, OK, PING, PONG, RELEASE, SET,  # noqa: E402
                         SPAWN, SUBSCRIBE, UNSUBSCRIBE, VALUE, WELCOME, Reader, _str, _value)

props = {"/sim/active-ship": 0, "/sim/paused": False, "/ships/ship[0]/name": "Mock Ship"}


def tick():
    t = time.time()
    props["/ships/ship[0]/altitude"] = 10000 + 500 * math.sin(t / 5)
    props["/ships/ship[0]/speed"] = 250 + 20 * math.sin(t / 3)


def frame(kind, request, payload=b""):
    return struct.pack("<IHI", 6 + len(payload), kind, request) + payload


def serve(conn):
    subs, buf, addon = {}, b"", "?"
    conn.settimeout(0.05)
    last = 0.0
    while True:
        tick()
        now = time.time()
        for path, hz in list(subs.items()):
            if now - last >= (1.0 / hz if hz else 0.1):
                conn.sendall(frame(VALUE, 0, _str(path) + _value(props.get(path))))
        if now - last >= 0.1:
            last = now
            conn.sendall(frame(EVENT, 0, _str("mfd") + _str("")))
        try:
            data = conn.recv(65536)
        except socket.timeout:
            continue
        if not data:
            return
        buf += data
        while len(buf) >= 4 and len(buf) >= 4 + struct.unpack_from("<I", buf)[0]:
            n = struct.unpack_from("<I", buf)[0]
            kind, rid = struct.unpack_from("<HI", buf, 4)
            r = Reader(buf[10:4 + n])
            buf = buf[4 + n:]
            if kind == HELLO:
                r.take("H")
                addon = r.str()
                print("hello from", addon)
                conn.sendall(frame(WELCOME, rid, struct.pack("<H", 1) + _str("mock") + struct.pack("<I", 63)))
            elif kind == PING:
                conn.sendall(frame(PONG, rid))
            elif kind == GET:
                path = r.str()
                conn.sendall(frame(VALUE, rid, _str(path) + _value(props.get(path))))
            elif kind == SET:
                path = r.str()
                props[path] = r.value()
                conn.sendall(frame(OK, rid, struct.pack("<I", 0)))
            elif kind == SUBSCRIBE:
                path = r.str()
                subs[path] = r.take("H")
            elif kind == UNSUBSCRIBE:
                subs.pop(r.str(), None)
            elif kind == LOG:
                print("[%s] %s" % (addon, r.str()))
            elif kind == DRAW:
                print("%s: page %s drawn" % (addon, r.str()))
            elif kind in (DEFINE_KEY, DEFINE_MFD, SPAWN, RELEASE, GIVE):
                print("%s: request type %d" % (addon, kind))
                conn.sendall(frame(OK, rid, struct.pack("<I", 1 if kind == SPAWN else 0)))


def run(conn):
    try:
        serve(conn)
    except OSError:
        pass


def main():
    server = socket.socket()
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", int(sys.argv[1]) if len(sys.argv) > 1 else 9420))
    server.listen(4)
    print("mock Kosmos on", server.getsockname())
    while True:
        conn, _ = server.accept()
        threading.Thread(target=lambda: (run(conn), conn.close()), daemon=True).start()


if __name__ == "__main__":
    main()
