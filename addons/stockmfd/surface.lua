-- Surface MFD: artificial horizon, heading tape and the surface-relative speeds.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function fmt_len(m)
  local a = math.abs(m)
  if a >= 1e6 then return string.format("%.2f Mm", m / 1e6) end
  if a >= 1e4 then return string.format("%.1f km", m / 1e3) end
  return string.format("%.0f m", m)
end

return {
  name = "Surface",
  buttons = { "", "", "", "", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    local pitch = ship("pitch")
    local heading = ship("heading")
    local cx, cy, r = 200, 190, 120
    local ppd = 4 -- pixels per degree of pitch

    g:colour(255, 240, 140)
    g:text(200, 14, "SURFACE  " .. ship("soi"), 1)

    -- The telemetry has no roll, so the horizon is drawn level.
    g:colour(90, 170, 255)
    g:rect(cx - r, cy - r, cx + r, cy + r)
    local hy = cy + pitch * ppd
    if hy > cy - r and hy < cy + r then
      g:colour(255, 200, 90)
      g:line(cx - r, hy, cx + r, hy)
    end
    g:colour(120, 255, 140)
    for deg = -80, 80, 10 do
      if deg ~= 0 then
        local y = cy + (pitch - deg) * ppd
        if y > cy - r + 6 and y < cy + r - 6 then
          g:line(cx - 22, y, cx + 22, y)
          g:text(cx + 26, y, tostring(deg), 0)
        end
      end
    end
    g:colour(255, 255, 255)
    g:line(cx - 30, cy, cx - 10, cy)
    g:line(cx + 10, cy, cx + 30, cy)
    g:circle(cx, cy, 3)

    local ty = cy + r + 18
    g:colour(120, 255, 140)
    g:line(cx - r, ty, cx + r, ty)
    for d = -40, 40 do
      local hd = (math.floor(heading) + d) % 360
      if hd % 5 == 0 then
        local x = cx + ((hd - heading + 540) % 360 - 180) * 2.6
        if x > cx - r and x < cx + r then
          g:line(x, ty, x, ty + (hd % 10 == 0 and 8 or 4))
          if hd % 30 == 0 then g:text(x, ty + 18, string.format("%03d", hd), 1) end
        end
      end
    end
    g:colour(255, 255, 255)
    g:tri(cx, ty - 1, cx - 5, ty - 10, cx + 5, ty - 10)
    g:text(cx, ty - 22, string.format("HDG %03.0f", heading), 1)

    g:colour(120, 255, 140)
    g:text(30, 36, "AGL " .. fmt_len(ship("altitude")), 0)
    g:text(30, 52, string.format("SPD %.1f m/s", ship("speed")), 0)
    g:text(370, 36, string.format("VS  %.1f m/s", ship("vertical-speed")), 2)
    g:text(370, 52, string.format("PIT %.1f", pitch), 2)
    g:text(200, 388, string.format("LAT %.3f  LON %.3f", ship("latitude"), ship("longitude")), 1)
  end,

  button = function(self, i) end,
}
