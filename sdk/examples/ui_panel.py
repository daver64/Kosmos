"""A small control panel: a window with preset buttons, a checkbox and a notice (KL_CAP_UI | CAP_WORLD)."""
from kosmos_link import CAP_UI, CAP_WORLD, Link, button, checkbox, label, parse_ui_event, separator, slider

link = Link("ui-panel", "Weather panel", CAP_UI | CAP_WORLD)
PRESETS = ["clear", "scattered", "overcast", "rain", "fog", "storm"]
state = {"gusty": False, "level": 0.0}


def show():
    widgets = [label("Set the weather"), separator()]
    widgets += [button(p, p.capitalize()) for p in PRESETS]
    widgets += [separator(), button("auto", "Back to automatic"), checkbox("notice", "Show a notice when I change it", state["gusty"]),
                slider("level", "Unused slider", state["level"], 0.0, 10.0)]
    link.ui_window("weather", "Weather panel", widgets)


def on_event(name, arg):
    if name != "ui":
        return
    window, widget, value = parse_ui_event(arg)
    if widget == "":
        raise SystemExit  # the player closed the window
    if widget == "notice":
        state["gusty"] = value == "1"
    elif widget == "level":
        state["level"] = float(value)
    elif widget == "auto":
        link.set_weather("auto")
    elif widget in PRESETS:
        link.set_weather("preset", widget)
        if state["gusty"]:
            link.notify("Weather set to " + widget)
    show()


link.on_event = on_event
show()
while True:
    link.poll(0.5)
