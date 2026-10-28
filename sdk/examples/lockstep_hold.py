"""Holds the flown ship's altitude with lockstep: the game waits for this program before each step (KL_CAP_READ | KL_CAP_CONTROLS)."""
from kosmos_link import CAP_CONTROLS, CAP_READ, Link, parse_step

link = Link("lockstep-hold", "Lockstep altitude hold", CAP_READ | CAP_CONTROLS)
ship = link.get("/sim/active-ship")
altitude = "/ships/ship[%d]/altitude" % ship
target = link.get(altitude)
state = {"throttle": 0.5, "last": target}


def on_event(name, arg):
    if name != "step":
        return
    frame, dt, _ = parse_step(arg)
    now = link.get(altitude)
    climb = (now - state["last"]) / dt
    state["last"] = now
    state["throttle"] = max(0.0, min(1.0, state["throttle"] + 0.00002 * (target - now) - 0.0004 * climb))
    link.set("/controls/throttle", state["throttle"])
    link.step_done(frame)  # the game now takes its step


link.on_event = on_event
link.lockstep(True, max_wait_ms=100, fixed_dt=1 / 60)
print("holding", round(target), "m")
while True:
    link.poll(0.5)
