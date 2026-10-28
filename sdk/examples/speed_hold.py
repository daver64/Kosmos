"""Holds the flown ship at 150 m/s by driving /controls/throttle: a tiny autopilot over KosmosLink."""
from kosmos_link import CAP_CONTROLS, CAP_READ, Link

TARGET = 150.0
link = Link("py-speed-hold", "Python speed hold", CAP_READ | CAP_CONTROLS)
throttle = 0.5


def on_value(path, speed):
    global throttle
    throttle = max(0.0, min(1.0, throttle + 0.0005 * (TARGET - speed)))
    link.set("/controls/throttle", throttle)


link.on_value = on_value
link.subscribe("/ships/ship[0]/speed", 20)
print("granted capabilities:", link.granted)
while True:
    link.poll(0.5)
