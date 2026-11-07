-- Ground clearance from the same terrain used for vessel contact.
local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function descent_warning(altitude, vertical)
  if vertical >= -1e-6 then return nil end
  local seconds = math.max(0, altitude) / -vertical
  if seconds <= 10 then return "GROUND: within 10 seconds" end
  if seconds <= 30 then return "GROUND: within 30 seconds" end
  return nil
end

return {
  name = "Radar Altimeter",
  _test = { descent_warning = descent_warning },
  buttons = { "", "", "", "", "", "", "", "", "", "", "", "" },
  init = function(self) end,
  draw = function(self, g, w, h)
    local altitude = ship("altitude")
    local vertical = ship("vertical-speed")
    local speed = ship("speed")
    local horizontal = math.sqrt(math.max(0, speed * speed - vertical * vertical))
    g:colour(255, 240, 140)
    g:text(200, 30, "RADAR ALTIMETER", 1)
    g:colour(120, 255, 140)
    g:text(200, 135, string.format("AGL %.1f m", math.max(0, altitude)), 1)
    g:text(200, 190, string.format("VERTICAL %+.1f m/s", vertical), 1)
    g:text(200, 230, string.format("GROUND %.1f m/s", horizontal), 1)
    g:colour(200, 210, 220)
    g:text(200, 340, "Estimated clearance", 1)
    local warning = descent_warning(altitude, vertical)
    local terrain_time = ship("terrain-warning-seconds")
    if terrain_time > 0 then
      g:colour(255, 120, 80)
      g:text(200, 275, string.format("TERRAIN AHEAD: ~%.0f s", terrain_time), 1)
    end
    if warning then
      g:colour(255, 120, 80)
      g:text(200, 300, warning, 1)
    end
    g:colour(200, 210, 220)
    if terrain_time < 0 then
      g:text(200, 320, "Terrain ahead unavailable", 1)
    else
      g:text(200, 320, "Sampled straight track, not a guarantee", 1)
    end
  end,
  button = function(self, i) end,
}
