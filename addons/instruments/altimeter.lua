-- Altimeter: a three-pointer clock face. The long needle makes one turn per kilometre (a digit is 100 m), the short needle one
-- turn per 10 km, and the small marker on the rim one turn per 100 km. The faces are the bitmaps in this folder.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function distance(m)
  if math.abs(m) >= 1.0e6 then return string.format("%.0f km", m / 1000) end
  if math.abs(m) >= 1.0e4 then return string.format("%.1f km", m / 1000) end
  return string.format("%.0f m", m)
end

return {
  name = "Altimeter",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h)
    local alt = math.max(0, ship("altitude"))
    g:image("alt_face.png", 200, 190, 300, 300)
    g:image("alt_marker_10km.png", 200, 190, 300, 300, (alt % 1.0e5) / 1.0e5 * 360)
    g:image("alt_needle_1km.png", 200, 190, 300, 300, (alt % 1.0e4) / 1.0e4 * 360)
    g:image("alt_needle_100m.png", 200, 190, 300, 300, (alt % 1.0e3) / 1.0e3 * 360)

    g:colour(255, 240, 140)
    g:text(200, 14, "ALTIMETER", 1)
    g:colour(120, 255, 140)
    g:text(200, 362, "ALT " .. distance(alt), 1)
    g:text(200, 382, string.format("VS %+.1f m/s", ship("vertical-speed")), 1)
  end,

  button = function(self, i) end,
}
