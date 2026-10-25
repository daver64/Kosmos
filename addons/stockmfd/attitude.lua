-- RCS Attitude MFD: attitude hold mode, throttle and the commanded attitude targets.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local modes = { "stability", "prograde", "retrograde", "radial", "antiradial", "normal", "antinormal", "target-attitude" }

return {
  name = "RCS Attitude",
  buttons = { "STAB", "PRO", "RETRO", "RAD+", "RAD-", "", "NRM+", "NRM-", "ATT", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "RCS ATTITUDE", 1)
    local hold = props.get("/controls/sas")
    g:colour(120, 255, 140)
    g:text(200, 44, "Hold: " .. hold, 1)
    g:text(200, 70, string.format("Pitch %6.1f   Heading %5.1f", ship("pitch"), ship("heading")), 1)
    -- Compass rose with the heading arrow.
    local cx, cy, r = 200, 220, 100
    g:circle(cx, cy, r)
    local hd = math.rad(ship("heading"))
    g:colour(255, 255, 255)
    g:line(cx, cy, cx + math.sin(hd) * r, cy - math.cos(hd) * r)
    g:colour(255, 200, 80)
    g:text(cx, cy - r - 10, "N", 1)
    g:text(cx + r + 10, cy, "E", 0)
    g:text(cx, cy + r + 12, "S", 1)
    g:text(cx - r - 10, cy, "W", 2)
    g:colour(120, 255, 140)
    g:text(200, 370, string.format("Throttle %3.0f%%", props.get("/controls/throttle") * 100), 1)
  end,

  button = function(self, i)
    local map = { [1] = "stability", [2] = "prograde", [3] = "retrograde", [4] = "radial", [5] = "antiradial", [7] = "normal", [8] = "antinormal" }
    if map[i] then
      props.set("/autopilot/enabled", false)
      props.set("/controls/sas", map[i])
    end
  end,
}
