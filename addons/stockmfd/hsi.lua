-- HSI MFD: a rotating compass card with the course arrow and deviation bar for a VOR or ILS on one NAV receiver.

local function n(path) return props.get(path) or 0 end

local function wrap180(a)
  a = (a + 180) % 360
  return a - 180
end

local function ship_heading()
  return n("/ships/ship[" .. n("/sim/active-ship") .. "]/heading")
end

return {
  name = "HSI",
  buttons = { "RCV", "OBS+", "OBS-", "", "", "", "", "", "", "+10", "-10", "" },
  init = function(self) self.rec = 0 end,

  draw = function(self, g, w, h, bare)
    local base = "/nav/nav[" .. self.rec .. "]/"
    local heading = ship_heading()
    local cx, cy, r = 200, 200, bare and 178 or 140
    -- On the small cockpit panel one-pixel strokes vanish, so draw each line several pixels wide.
    local line = g.line
    local function stroke(x1, y1, x2, y2)
      if not bare then return line(g, x1, y1, x2, y2) end
      local dx, dy = x2 - x1, y2 - y1
      local len = math.sqrt(dx * dx + dy * dy)
      if len < 0.001 then return end
      local nx, ny = -dy / len, dx / len
      for o = -3, 3 do line(g, x1 + nx * o, y1 + ny * o, x2 + nx * o, y2 + ny * o) end
    end
    local function at(bearing, radius)
      local a = math.rad(bearing - heading)
      return cx + math.sin(a) * radius, cy - math.cos(a) * radius
    end
    if not bare then
      g:colour(255, 240, 140)
      g:text(200, 14, "HSI", 1)
      g:colour(120, 200, 255)
      g:text(8, 14, string.format("NAV%d %.2f", self.rec + 1, n(base .. "frequency")), 0)
    end

    g:colour(80, 110, 200)
    if bare then
      for b = 0, 355, 5 do
        local x1, y1 = at(b, r)
        local x2, y2 = at(b + 5, r)
        stroke(x1, y1, x2, y2)
      end
    else
      g:circle(cx, cy, r)
    end
    for b = 0, 350, 10 do
      local x1, y1 = at(b, r)
      local x2, y2 = at(b, r - (b % 30 == 0 and 14 or 7) * (bare and 2 or 1))
      stroke(x1, y1, x2, y2)
      if b % 30 == 0 and not bare then
        local label = ({ [0] = "N", [90] = "E", [180] = "S", [270] = "W" })[b] or tostring(b // 10)
        local tx, ty = at(b, r - 26)
        g:colour(200, 210, 220)
        g:text(tx, ty, label, 1)
        g:colour(80, 110, 200)
      end
    end
    g:colour(255, 255, 255)
    g:tri(cx, cy - r - 2, cx - 6, cy - r - 14, cx + 6, cy - r - 14)
    stroke(cx, cy - 18, cx, cy + 18)
    stroke(cx - 18, cy + 6, cx + 18, cy + 6)

    if not props.get(base .. "valid") then
      if not bare then
        g:colour(255, 200, 80)
        g:text(200, 372, "No signal. Tune with COM/NAV.", 1)
      end
      return
    end
    local kind = props.get(base .. "type")
    g:colour(120, 255, 140)
    if not bare then g:text(200, 372, (props.get(base .. "ident") or "") .. string.format("  %.1f km", n(base .. "distance") / 1000), 1) end

    -- Bearing pointer to the transmitter.
    local bx, by = at(n(base .. "bearing"), r - 40)
    g:colour(255, 160, 90)
    stroke(cx, cy, bx, by)
    g:disc(bx, by, 4)

    local course, dev, scale, flag = nil, 0, 10, ""
    if kind == "ils" and props.get(base .. "ils/valid") then
      course = n(base .. "ils/course")
      dev = n(base .. "ils/localizer")
      scale = 40
      flag = "ILS"
      local gs = n(base .. "ils/glideslope")
      g:colour(255, 100, 255)
      local gy = cy + math.max(-100, math.min(100, gs * 40))
      stroke(cx - 150, gy, cx - 120, gy)
      stroke(cx + 120, gy, cx + 150, gy)
    elseif kind == "vor" then
      course = n(base .. "obs")
      local radial = n(base .. "radial")
      if math.abs(wrap180(radial - course)) < 90 then
        dev = -wrap180(radial - course)
        flag = "FROM"
      else
        dev = wrap180(radial - (course + 180))
        flag = "TO"
      end
    end
    if not course then
      g:colour(255, 200, 80)
      if not bare then g:text(200, 390, "Receiver tuned to " .. string.upper(kind or "") .. " (no course)", 1) end
      return
    end
    local a = math.rad(course - heading)
    local ux, uy = math.sin(a), -math.cos(a)
    local vx, vy = math.cos(a), math.sin(a)
    local off = math.max(-100, math.min(100, dev * scale))
    g:colour(255, 120, 255)
    stroke(cx - ux * 100, cy - uy * 100, cx + ux * 100, cy + uy * 100)
    g:tri(cx + ux * 112, cy + uy * 112, cx + ux * 90 + vx * 8, cy + uy * 90 + vy * 8, cx + ux * 90 - vx * 8, cy + uy * 90 - vy * 8)
    g:colour(255, 255, 120)
    local bx0, by0 = cx + vx * off, cy + vy * off
    stroke(bx0 - ux * 60, by0 - uy * 60, bx0 + ux * 60, by0 + uy * 60)
    if bare then return end
    g:colour(200, 210, 220)
    g:text(8, 392, string.format("CRS %03d  %s", math.floor(course + 0.5) % 360, flag), 0)
    g:text(392, 392, string.format("HDG %03d", math.floor(heading + 0.5) % 360), 2)
  end,

  button = function(self, i)
    local path = "/nav/nav[" .. self.rec .. "]/obs"
    local obs = n(path)
    if i == 1 then self.rec = 1 - self.rec
    elseif i == 2 then props.set(path, (obs + 1) % 360)
    elseif i == 3 then props.set(path, (obs - 1) % 360)
    elseif i == 10 then props.set(path, (obs + 10) % 360)
    elseif i == 11 then props.set(path, (obs - 10) % 360)
    end
  end,
}
