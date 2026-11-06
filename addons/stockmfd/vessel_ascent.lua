local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end
local ap = "/autopilot/vessel-ascent/"
local profiles = {
  ["Ares Spider"] = "Ares",
  ["CVEL Deltaglider"] = "Deltaglider",
  ["CVEL Deltaglider (Nuclear)"] = "Deltaglider",
  ["CVEL Shuttle-A"] = "Shuttle-A",
  ["CVEL Shuttle-A (Nuclear)"] = "Shuttle-A",
  ["CVEL Shuttle-AQ"] = "Shuttle-A",
}
return {
  name = "Vessel Ascent",
  buttons = { "START", "STOP", "", "", "", "", "ALT+", "ALT-", "HDG+", "HDG-", "", "" },
  init = function(self) end,
  draw = function(self, g)
    local name = profiles[ship("class")] or "Unsupported"
    g:colour(255, 240, 140)
    g:text(200, 18, string.upper(name) .. " ASCENT", 1)
    g:colour(120, 255, 140)
    g:text(200, 65, props.get(ap .. "state") or "Off", 1)
    g:text(30, 115, string.format("TARGET %.0f km", props.get(ap .. "target-km") or 200), 0)
    g:text(30, 145, string.format("HEADING %03.0f", props.get(ap .. "heading") or 90), 0)
    g:text(30, 190, string.format("ALT %.1f km", ship("altitude") / 1000), 0)
    g:text(30, 220, string.format("AP %.1f km", ship("orbit/apoapsis") / 1000), 0)
    g:text(30, 250, string.format("PE %.1f km", ship("orbit/periapsis") / 1000), 0)
    g:text(30, 280, string.format("Q %.1f kPa", ship("dynamic-pressure") / 1000), 0)
    g:colour(255, 200, 80)
    if props.get(ap .. "active") then
      g:text(200, 315, "TARGET / HEADING LOCKED", 1)
    end
    g:text(200, 345, "MONITOR AND TAKE OVER IF NEEDED", 1)
  end,
  button = function(self, i)
    if i == 1 then
      props.set(ap .. "request", profiles[ship("class")] or "unsupported")
    elseif i == 2 then
      props.set(ap .. "request", "stop")
    elseif (i == 7 or i == 8) and not props.get(ap .. "active") then
      props.set(ap .. "target-km", math.max(150, math.min(1000, props.get(ap .. "target-km") + (i == 7 and 10 or -10))))
    elseif (i == 9 or i == 10) and not props.get(ap .. "active") then
      props.set(ap .. "heading", (props.get(ap .. "heading") + (i == 9 and 5 or -5)) % 360)
    end
  end,
}
