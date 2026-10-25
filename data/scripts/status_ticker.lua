-- Timer script: reports the active ship every few seconds. `dt` is the time since the last run.
local base = "/ships/ship[" .. props.get("/sim/active-ship") .. "]"
print(string.format("%s: alt %.1f km, speed %.1f m/s", props.get(base .. "/name"), (props.get(base .. "/altitude") or 0) / 1000, props.get(base .. "/speed") or 0))
