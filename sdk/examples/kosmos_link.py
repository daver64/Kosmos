"""A pure-Python KosmosLink client (protocol version 2). One file, standard library only."""
import os
import socket
import struct
import time

CAP_READ, CAP_CONTROLS, CAP_WRITE, CAP_SPAWN, CAP_DRAW, CAP_KEYS, CAP_WORLD = 1, 2, 4, 8, 16, 32, 64
(HELLO, SUBSCRIBE, UNSUBSCRIBE, GET, SET, DEFINE_KEY, SPAWN, RELEASE, GIVE, DEFINE_MFD, DRAW, LOG, PING) = range(1, 14)
(WEATHER_AT, SET_WEATHER, SUBSCRIBE_EVENT, UNSUBSCRIBE_EVENT, LIST_SHIPS, REMOVE_SHIP, PLACE_SHIP, LIST_FACILITIES) = range(14, 22)
WELCOME, ERROR, VALUE, EVENT, OK, PONG, WEATHER, SHIP_LIST, FACILITY_LIST = 101, 102, 103, 104, 105, 106, 107, 108, 109
LINE, RECT, FILLRECT, CIRCLE, DISC, TRI, TEXT, IMAGE = range(8)


class LinkError(Exception):
    pass


def _str(s):
    b = s.encode()
    return struct.pack("<H", len(b)) + b


def _value(v):
    if isinstance(v, bool):
        return struct.pack("<BB", 1, v)
    if isinstance(v, int):
        return struct.pack("<Bi", 2, v)
    if isinstance(v, float):
        return struct.pack("<Bd", 3, v)
    return b"\x04" + _str(str(v))


class Reader:
    def __init__(self, data):
        self.d, self.i = data, 0

    def take(self, fmt):
        out = struct.unpack_from("<" + fmt, self.d, self.i)
        self.i += struct.calcsize("<" + fmt)
        return out if len(out) > 1 else out[0]

    def str(self):
        n = self.take("H")
        s = self.d[self.i:self.i + n].decode()
        self.i += n
        return s

    def value(self):
        tag = self.take("B")
        return {0: lambda: None, 1: lambda: bool(self.take("B")), 2: lambda: self.take("i"),
                3: lambda: self.take("d"), 4: self.str}[tag]()


class Link:
    def __init__(self, addon_id, name, capabilities, version="1.0", address=None, token=None):
        address = address or os.environ.get("KOSMOS_LINK", "127.0.0.1:9420")
        host, port = address.rsplit(":", 1)
        self.sock = socket.create_connection((host, int(port)), 5)
        self.buf = b""
        self.next_id = 1
        self.on_value = None  # f(path, value)
        self.on_event = None  # f(name, argument)
        payload = struct.pack("<H", 2) + _str(addon_id) + _str(name) + _str(version) \
            + _str(token or os.environ.get("KOSMOS_LINK_TOKEN", "")) + struct.pack("<I", capabilities)
        kind, r = self._request(HELLO, payload)
        r.take("H")
        self.sim_version = r.str()
        self.granted = r.take("I")

    def _send(self, kind, payload=b"", request=0):
        self.sock.sendall(struct.pack("<IHI", 6 + len(payload), kind, request) + payload)

    def _frame(self, timeout):
        self.sock.settimeout(timeout)
        while True:
            if len(self.buf) >= 4:
                n = struct.unpack_from("<I", self.buf)[0]
                if len(self.buf) >= 4 + n:
                    frame, self.buf = self.buf[4:4 + n], self.buf[4 + n:]
                    kind, request = struct.unpack_from("<HI", frame)
                    return kind, request, Reader(frame[6:])
            try:
                chunk = self.sock.recv(65536)
            except (socket.timeout, BlockingIOError):
                return None
            if not chunk:
                raise LinkError("connection closed")
            self.buf += chunk

    def _dispatch(self, kind, request, r):
        if kind == VALUE and request == 0 and self.on_value:
            self.on_value(r.str(), r.value())
        elif kind == EVENT and self.on_event:
            self.on_event(r.str(), r.str())

    def _request(self, kind, payload=b"", timeout=5.0):
        rid, self.next_id = self.next_id, self.next_id + 1
        self._send(kind, payload, rid)
        deadline = time.time() + timeout
        while time.time() < deadline:
            frame = self._frame(max(0.01, deadline - time.time()))
            if not frame:
                break
            k, request, r = frame
            if request == rid:
                if k == ERROR:
                    raise LinkError("%d: %s" % (r.take("H"), r.str()))
                return k, r
            self._dispatch(k, request, r)
        raise LinkError("timeout")

    def poll(self, timeout=0.0):
        """Runs the handlers for whatever arrives within timeout seconds."""
        while True:
            frame = self._frame(timeout)
            if not frame:
                return
            self._dispatch(*frame)
            timeout = 0.0

    def get(self, path):
        _, r = self._request(GET, _str(path))
        r.str()
        return r.value()

    def set(self, path, value):
        self._request(SET, _str(path) + _value(value))

    def subscribe(self, path, hz=10):
        self._send(SUBSCRIBE, _str(path) + struct.pack("<H", hz))

    def define_key(self, id, label, group, key, shift=False, hold=False):
        self._request(DEFINE_KEY, _str(id) + _str(label) + _str(group) + _str(key) + struct.pack("<BB", shift, 2 if hold else 1))

    def spawn_ship(self, name, x=0.0, y=0.0, z=0.0):
        return self._request(SPAWN, _str(name) + struct.pack("<ddd", x, y, z))[1].take("I")

    def release(self, hardpoint=-1):
        self._request(RELEASE, struct.pack("<i", hardpoint))

    def give(self, equipment, count=1):
        self._request(GIVE, _str(equipment) + struct.pack("<i", count))

    def define_mfd(self, id, title):
        self._request(DEFINE_MFD, _str(id) + _str(title))

    def draw(self, id, commands):
        """commands: tuples (kind, a, b, c, d, e, f, r, g, b, align, text)."""
        payload = _str(id) + struct.pack("<H", len(commands))
        for k, a, b, c, d, e, f, r, g, bl, align, text in commands:
            payload += struct.pack("<B6fBBBB", k, a, b, c, d, e, f, r, g, bl, align) + _str(text)
        self._send(DRAW, payload)

    def log(self, text):
        self._send(LOG, _str(text))

    # ---- Protocol 2

    def weather_at(self, latitude, longitude, altitude=0.0, body=""):
        """Weather at a place (altitude in metres above the ground; body "" is the flown ship's). Returns a dict."""
        _, r = self._request(WEATHER_AT, _str(body) + struct.pack("<ddd", latitude, longitude, altitude))
        keys = ("wind_east", "wind_north", "wind_speed", "wind_from", "gust", "rain", "thunder", "cloud_cover",
                "cloud_base", "cloud_top", "visibility", "temperature", "pressure")
        out = {k: r.take("d") for k in keys}
        out["station"], out["metar"] = r.str(), r.str()
        return out

    def set_weather(self, mode, arg=""):
        """mode: "metar" (arg is a METAR line), "preset" (arg is a preset id) or "auto". Needs CAP_WORLD."""
        self._request(SET_WEATHER, _str(mode) + _str(arg))

    def subscribe_event(self, name):
        """Asks for an opt-in event: pause, warp, scenario, soi, crash, touchdown, weather, ships or tick."""
        self._request(SUBSCRIBE_EVENT, _str(name))

    def unsubscribe_event(self, name):
        self._request(UNSUBSCRIBE_EVENT, _str(name))

    def ships(self, radius=0.0, buildings=False):
        """Ships within radius metres of the flown one (0: all), as a list of dicts. flags: 1 flown, 2 landed, 4 crashed, 8 spawned by a link."""
        _, r = self._request(LIST_SHIPS, struct.pack("<dB", radius, buildings))
        out = []
        for _ in range(r.take("I")):
            s = {"index": r.take("I"), "name": r.str(), "body": r.str()}
            for k in ("latitude", "longitude", "altitude", "heading", "speed", "vertical_speed", "distance"):
                s[k] = r.take("d")
            s["flags"] = r.take("B")
            out.append(s)
        return out

    def remove_ship(self, index):
        """Deletes a ship an addon spawned. Needs CAP_WORLD."""
        self._request(REMOVE_SHIP, struct.pack("<I", index))

    def place_ship(self, index, latitude, longitude, altitude=0.0, heading=0.0, speed=0.0, body=""):
        """Moves a ship; altitude 0 puts it on the ground. Needs CAP_WORLD."""
        self._request(PLACE_SHIP, struct.pack("<I", index) + _str(body) + struct.pack("<5d", latitude, longitude, altitude, heading, speed))

    def facilities(self, latitude, longitude, radius_km=0.0, kind="", body="", limit=100):
        """Bases near a point, nearest first, as a list of dicts with runways and navaids."""
        _, r = self._request(LIST_FACILITIES, _str(kind) + _str(body) + struct.pack("<dddH", latitude, longitude, radius_km, limit))
        out = []
        for _ in range(r.take("I")):
            f = {"id": r.str(), "name": r.str(), "icao": r.str(), "body": r.str()}
            for k in ("latitude", "longitude", "elevation", "heading", "distance"):
                f[k] = r.take("d")
            f["runways"], f["navaids"] = [], []
            for _ in range(r.take("H")):
                rw = {"name": r.str()}
                for k in ("latitude", "longitude", "heading", "length", "width"):
                    rw[k] = r.take("d")
                rw["ils"] = [r.take("d") for _ in range(r.take("B"))]
                f["runways"].append(rw)
            for _ in range(r.take("H")):
                nv = {"type": r.str(), "id": r.str(), "name": r.str()}
                for k in ("latitude", "longitude", "frequency", "range"):
                    nv[k] = r.take("d")
                f["navaids"].append(nv)
            out.append(f)
        return out
