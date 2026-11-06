-- Orbit MFD: the orbit in its own plane with the apsides, plus the main orbital elements.
-- An MFD mode is a table: name, buttons (12 labels), init(self), update(self, dt), draw(self, g, w, h), button(self, i).
-- g draws in a 400x400 box: colour, line, rect, fillrect, circle, disc, tri, arc, text(x, y, s, align 0/1/2).

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function fmt_len(m)
  local a = math.abs(m)
  if a >= 1e9 then return string.format("%.2f Gm", m / 1e9) end
  if a >= 1e6 then return string.format("%.1f Mm", m / 1e6) end
  if a >= 1e3 then return string.format("%.1f km", m / 1e3) end
  return string.format("%.0f m", m)
end

local function fmt_time(s)
  if s <= 0 then return "--" end
  local h = math.floor(s / 3600)
  local m = math.floor((s % 3600) / 60)
  return string.format("%d:%02d:%02d", h, m, math.floor(s % 60))
end

return {
  name = "Orbit",
  buttons = { "", "", "", "", "", "", "", "", "", "", "", "" },

  init = function(self) end,

  draw = function(self, g, w, h)
    local bound = ship("orbit/bound")
    local e = ship("orbit/eccentricity")
    local a = ship("orbit/semi-major-axis")
    local radius = ship("body-radius")
    local rp = ship("orbit/periapsis") + radius
    local ra = bound and (ship("orbit/apoapsis") + radius) or 0
    local nu = math.rad(ship("orbit/true-anomaly"))

    g:colour(255, 240, 140)
    g:text(200, 14, "ORBIT  " .. ship("soi"), 1)

    local cx, cy = 200, 205
    local p = (e < 1) and (a * (1 - e * e)) or (-a * (e * e - 1))
    local sr = p / (1 + e * math.cos(nu))
    -- On an escape path the plot grows to keep the ship on the display instead of letting it run off.
    local extent = bound and ra or math.max(rp * 4, sr * 1.15)
    local scale = 135 / math.max(extent, radius * 1.05)
    g:colour(60, 110, 255)
    g:circle(cx, cy, radius * scale)

    g:colour(120, 255, 140)
    local lo, hi = 0, 2 * math.pi
    if not bound and e > 1 then hi = math.acos(-1 / e) * 0.97; lo = -hi end
    local px, py
    local steps = 90
    for i = 0, steps do
      local t = lo + (hi - lo) * i / steps
      local r = p / (1 + e * math.cos(t))
      if r > 0 and r < extent * 1.5 then
        local x, y = cx + r * math.cos(t) * scale, cy - r * math.sin(t) * scale
        if px then g:line(px, py, x, y) end
        px, py = x, y
      else
        px = nil
      end
    end

    g:colour(255, 200, 80)
    g:circle(cx + rp * scale, cy, 4)
    g:text(cx + rp * scale + 8, cy - 8, "Pe", 0)
    if bound then
      g:circle(cx - ra * scale, cy, 4)
      g:text(cx - ra * scale - 8, cy - 8, "Ap", 2)
    end
    local sx, sy = cx + sr * math.cos(nu) * scale, cy - sr * math.sin(nu) * scale
    g:colour(255, 255, 255)
    g:disc(sx, sy, 4)

    g:colour(120, 255, 140)
    local rows = {
      { "PeA", fmt_len(ship("orbit/periapsis")) }, { "ApA", bound and fmt_len(ship("orbit/apoapsis")) or "--" },
      { "Ecc", string.format("%.4f", e) }, { "Inc", string.format("%.2f", ship("orbit/inclination")) },
    }
    for i, row in ipairs(rows) do
      g:text(70 + (i - 1) % 2 * 190, 360 + math.floor((i - 1) / 2) * 16, row[1] .. " " .. row[2], 0)
    end
    if not bound and e > 1 then
      g:colour(255, 200, 80)
      local v = ship("orbital-speed")
      local mu = v * v / (2 / sr - 1 / a)
      g:text(30, 68, "ESCAPING", 0)
      g:text(30, 84, string.format("v inf %.0f m/s", math.sqrt(math.max(-mu / a, 0))), 0)
      g:colour(120, 255, 140)
    end
    g:text(30, 36, "T " .. fmt_time(ship("orbit/period")), 0)
    g:text(30, 52, "Tpe " .. fmt_time(ship("orbit/time-to-periapsis")), 0)
    if bound then g:text(30, 68, "Tap " .. fmt_time(ship("orbit/time-to-apoapsis")), 0) end
    g:text(370, 36, string.format("Orb %.0f m/s", ship("orbital-speed")), 2)
    g:text(370, 52, "AGL " .. fmt_len(ship("altitude")), 2)
  end,

  button = function(self, i) end,
}
