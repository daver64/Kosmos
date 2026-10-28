"""A vessel module: spawns a ship, takes it over (KL_CAP_VESSEL | KL_CAP_SPAWN | KL_CAP_READ) and keeps it climbing gently with its own throttle and a nudge of force."""
from kosmos_link import CAP_READ, CAP_SPAWN, CAP_VESSEL, Link

link = Link("vessel-hover", "Hover module", CAP_READ | CAP_SPAWN | CAP_VESSEL)
link.spawn_ship("CVEL Deltaglider", 0.0, 0.0, -60.0)
ships = [s for s in link.ships() if s["flags"] & 8]  # the ones a link created
index = ships[-1]["index"]
link.claim_ship(index)
print("owning ship", index, ships[-1]["name"])
while True:
    link.ship_value(index, "controls/throttle", 0.6)
    link.ship_value(index, "controls/gear", 0.0)
    link.ship_force(index, force=(0.0, 2000.0, 0.0))  # N, in the ship's own axes
    link.poll(0.5)
