#!/usr/bin/env python3
"""Picks the 64 METAR stations that cover the Earth best and writes data/weather/earth_stations.json.

Greedy farthest-point sampling over the stations that reported in the last hour (NOAA's current METAR cache): each
pick is the reporting station furthest from all those already chosen, so the set spreads over land and islands alike.
Run it again to refresh the list; the shipped file is what the game reads.

    tools/pick_weather_stations.py [-n 64] [--seed EFHK,...] [--cache metars.cache.csv.gz]
"""
import argparse
import csv
import gzip
import io
import json
import math
import os
import re
import urllib.request

URL = "https://aviationweather.gov/data/cache/metars.cache.csv.gz"


def unit(lat, lon):
    la, lo = math.radians(lat), math.radians(lon)
    return (math.cos(la) * math.cos(lo), math.cos(la) * math.sin(lo), math.sin(la))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", type=int, default=64)
    ap.add_argument("--seed", default="EFHK,KJFK,YSWG", help="stations always included (Helsinki, New York, Wagga Wagga)")
    ap.add_argument("--cache", help="a downloaded metars.cache.csv.gz instead of fetching it")
    ap.add_argument("--max-elevation", type=float, default=1500.0)
    args = ap.parse_args()

    raw = open(args.cache, "rb").read() if args.cache else urllib.request.urlopen(URL, timeout=60).read()
    text = gzip.decompress(raw).decode("utf-8", "replace")
    lines = text.split("\n")
    start = next(i for i, l in enumerate(lines) if l.startswith("raw_text,"))
    stations = {}
    for row in csv.DictReader(io.StringIO("\n".join(lines[start:]))):
        sid = row.get("station_id", "")
        if not re.fullmatch(r"[A-Z]{4}", sid) or row.get("metar_type") not in ("METAR", "SPECI"):
            continue
        if row.get("maintenance_indicator_on") == "TRUE":
            continue
        try:
            lat, lon, elev = float(row["latitude"]), float(row["longitude"]), float(row["elevation_m"] or 0)
        except ValueError:
            continue
        if elev > args.max_elevation:
            continue
        stations[sid] = (lat, lon, elev, unit(lat, lon))

    chosen = [s for s in args.seed.split(",") if s in stations]
    best = {sid: 9.0 for sid in stations}

    def add(sid):
        u = stations[sid][3]
        for other, (_, _, _, v) in stations.items():
            d = math.acos(max(-1.0, min(1.0, u[0] * v[0] + u[1] * v[1] + u[2] * v[2])))
            best[other] = min(best[other], d)

    for sid in chosen:
        add(sid)
    while len(chosen) < args.n:
        sid = max(best, key=best.get)
        chosen.append(sid)
        add(sid)
    worst = max(best.values()) * 6371.0
    out = [{"id": sid, "latitude": round(stations[sid][0], 3), "longitude": round(stations[sid][1], 3), "elevation": stations[sid][2]} for sid in chosen]
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "weather", "earth_stations.json")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump({"note": "Chosen by tools/pick_weather_stations.py", "stations": out}, f, indent=1)
        f.write("\n")
    print("%d candidates, %d chosen, furthest point from any station %.0f km" % (len(stations), len(chosen), worst))


if __name__ == "__main__":
    main()
