-- VTOL MFD: guidance to a pad beacon. The pad is shown relative to the ship, with the ship's heading up, together with
-- the ship's velocity over the pad, its height and its vertical speed.

local function n(path) return props.get(path) or 0 end

return {
  name = "VTOL",
  buttons = { "RCV", "ZOOM+", "ZOOM-", "", "", "", "", "", "", "", "", "" },
  init = function(self)
    self.rec = 0
    self.zoom = 1.0
  end,

  draw = function(self, g, w, h)
    local base = "/nav/nav[" .. self.rec .. "]/"
    g:colour(255, 240, 140)
    g:text(200, 14, "VTOL", 1)
    g:colour(120, 200, 255)
    g:text(8, 14, string.format("NAV%d %.2f", self.rec + 1, n(base .. "frequency")), 0)
    if not props.get(base .. "valid") or not props.get(base .. "vtol/valid") then
      g:colour(255, 200, 80)
      g:text(200, 200, "No VTOL beacon", 1)
      g:text(200, 222, "Tune a VTOL frequency in COM/NAV", 1)
      return
    end
    local x, y = n(base .. "vtol/x"), n(base .. "vtol/y")
    local vx, vy = n(base .. "vtol/vx"), n(base .. "vtol/vy")
    local height, vv = n(base .. "vtol/height"), n(base .. "vtol/vertical-speed")
    local dist = math.sqrt(x * x + y * y)
    local cx, cy, half = 200, 180, 130
    -- Full scale is the distance to the pad, at least 20 m, widened or narrowed by the zoom.
    local span = math.max(20.0, dist * 1.3) / self.zoom
    local function px(a) return math.max(-half, math.min(half, a / span * half)) end

    g:colour(60, 110, 255)
    g:rect(cx - half, cy - half, 2 * half, 2 * half)
    g:line(cx - half, cy, cx + half, cy)
    g:line(cx, cy - half, cx, cy + half)
    g:circle(cx, cy, half / 2)

    -- The pad as a square turned by its heading relative to ours, with a tick on its nose.
    local pa = math.rad(n(base .. "vtol/pad-heading"))
    local px0, py0 = cx + px(x), cy - px(y)
    local size = math.max(4, 15 / span * half)
    local function corner(dx, dy)
      return px0 + dx * math.cos(pa) + dy * math.sin(pa), py0 + dy * math.cos(pa) - dx * math.sin(pa)
    end
    g:colour(120, 255, 140)
    local x1, y1 = corner(-size, -size)
    local x2, y2 = corner(size, -size)
    local x3, y3 = corner(size, size)
    local x4, y4 = corner(-size, size)
    g:line(x1, y1, x2, y2)
    g:line(x2, y2, x3, y3)
    g:line(x3, y3, x4, y4)
    g:line(x4, y4, x1, y1)
    local nx, ny = corner(0, -size * 1.8)
    g:line(px0, py0, nx, ny)

    -- Velocity over the pad: a line from the centre, 10 px per m/s.
    g:colour(255, 160, 90)
    local lx = math.max(-half, math.min(half, vx * 10))
    local ly = math.max(-half, math.min(half, vy * 10))
    g:line(cx, cy, cx + lx, cy - ly)
    g:disc(cx + lx, cy - ly, 3)

    g:colour(120, 255, 140)
    g:text(8, 330, string.format("Height  %8.1f m", height), 0)
    if vv < -5 and height < 100 then g:colour(255, 120, 100) end
    g:text(8, 346, string.format("V.speed %8.2f m/s", vv), 0)
    g:colour(120, 255, 140)
    g:text(8, 362, string.format("Pad     %8.1f m", dist), 0)
    g:text(220, 330, string.format("Ground  %5.2f m/s", math.sqrt(vx * vx + vy * vy)), 0)
    g:text(220, 346, string.format("Scale   %5.0f m", span), 0)
    g:colour(200, 210, 220)
    g:text(200, 386, props.get(base .. "name") or "", 1)
  end,

  button = function(self, i)
    if i == 1 then self.rec = 1 - self.rec
    elseif i == 2 then self.zoom = math.min(32.0, self.zoom * 2)
    elseif i == 3 then self.zoom = math.max(0.125, self.zoom / 2)
    end
  end,
}
