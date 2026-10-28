"""Draws markers and a flight path in the 3D view: a ring and a label on the nearest airport and a line from the ship to it (KL_CAP_READ | KL_CAP_DRAW3D)."""
from kosmos_link import CAP_DRAW3D, CAP_READ, Link, label3, line3, marker3, ring3

link = Link("waypoints3d", "3D waypoints", CAP_READ | CAP_DRAW3D)
ship = link.get("/sim/active-ship")
base = "/ships/ship[%d]/" % ship
airports = link.facilities(link.get(base + "latitude"), link.get(base + "longitude"), kind="airport", limit=3)
GREEN, AMBER = (120, 255, 150, 255), (255, 200, 90, 200)
while True:
    items = []
    for a in airports:
        at = (a["latitude"], a["longitude"], a["elevation"] + 5.0)
        items += [ring3(at, 400.0, rgba=AMBER), marker3(at, 8.0, rgba=GREEN), label3(at, "%s %s" % (a["icao"], a["name"]), rgba=GREEN)]
    if airports:
        a = airports[0]
        here = (link.get(base + "latitude"), link.get(base + "longitude"), link.get(base + "altitude"))
        items.append(line3(here, (a["latitude"], a["longitude"], a["elevation"] + 5.0), rgba=GREEN))
    link.draw3d("airports", items)
    link.poll(0.5)
