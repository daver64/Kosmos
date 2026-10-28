"""Prints the three nearest airports to the flown ship, with their runways, and the weather at the nearest one.

Run it with Kosmos started (or tools/kosmos_link_sim.py): python3 nearest_runway.py
"""
from kosmos_link import CAP_READ, Link

link = Link("nearest-runway", "Nearest runway", CAP_READ)
ship = link.get("/sim/active-ship")
base = "/ships/ship[%d]/" % ship
lat, lon = link.get(base + "latitude"), link.get(base + "longitude")
airports = link.facilities(lat, lon, kind="airport", limit=3)
for a in airports:
    print("%s (%s)  %.0f km" % (a["name"], a["icao"], a["distance"] / 1000))
    for r in a["runways"]:
        ils = ", ".join("%.2f" % f for f in r["ils"]) or "none"
        print("  %s  heading %03.0f  %.0f m  ILS %s" % (r["name"], r["heading"], r["length"], ils))
if airports:
    w = link.weather_at(airports[0]["latitude"], airports[0]["longitude"])
    print("Weather there: wind %03.0f at %.0f m/s, visibility %.0f m, %.0f C" % (w["wind_from"], w["wind_speed"], w["visibility"], w["temperature"]))
