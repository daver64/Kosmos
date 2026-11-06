-- Ground clearance from the same terrain used for vessel contact.
local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Radar Altimeter",
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
  end,
  button = function(self, i) end,
}
