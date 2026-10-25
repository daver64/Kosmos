-- Map MFD: latitude/longitude grid of the body you are over, with the ship's ground track.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local MAX_POINTS = 240

return {
  name = "Map",
  buttons = { "", "", "", "", "", "", "", "", "", "", "", "" },

  init = function(self)
    self.track = {}
    self.timer = 0
    self.body = ""
  end,

  update = function(self, dt)
    local body = ship("soi")
    if body ~= self.body then
      self.track = {}
      self.body = body
    end
    self.timer = self.timer - dt
    if self.timer <= 0 then
      self.timer = 5
      local t = self.track
      t[#t + 1] = { ship("longitude"), ship("latitude") }
      if #t > MAX_POINTS then table.remove(t, 1) end
    end
  end,

  draw = function(self, g, w, h)
    local x0, y0, mw, mh = 20, 110, 360, 180
    local function mx(lon) return x0 + (lon + 180) / 360 * mw end
    local function my(lat) return y0 + (90 - lat) / 180 * mh end

    g:colour(255, 240, 140)
    g:text(200, 14, "MAP  " .. ship("soi"), 1)

    g:colour(60, 120, 80)
    for lon = -150, 150, 30 do g:line(mx(lon), y0, mx(lon), y0 + mh) end
    for lat = -60, 60, 30 do g:line(x0, my(lat), x0 + mw, my(lat)) end
    g:colour(100, 200, 130)
    g:line(x0, my(0), x0 + mw, my(0))
    g:rect(x0, y0, x0 + mw, y0 + mh)

    g:colour(255, 200, 80)
    local prev
    for _, p in ipairs(self.track) do
      if prev and math.abs(p[1] - prev[1]) < 180 then g:line(mx(prev[1]), my(prev[2]), mx(p[1]), my(p[2])) end
      prev = p
    end

    local lon, lat = ship("longitude"), ship("latitude")
    g:colour(255, 255, 255)
    local sx, sy = mx(lon), my(lat)
    g:circle(sx, sy, 5)
    g:line(sx - 9, sy, sx + 9, sy)
    g:line(sx, sy - 9, sx, sy + 9)

    g:colour(120, 255, 140)
    g:text(20, 310, string.format("LAT %7.3f", lat), 0)
    g:text(20, 326, string.format("LON %8.3f", lon), 0)
    g:text(380, 310, string.format("ALT %.1f km", ship("altitude") / 1000), 2)
    g:text(380, 326, string.format("INC %.2f", ship("orbit/inclination")), 2)
  end,

  button = function(self, i) end,
}
