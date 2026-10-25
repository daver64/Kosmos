-- VOR/ILS MFD: both NAV receivers as course-deviation dials, with the course (OBS) set per receiver.

local function n(path) return props.get(path) or 0 end

local function wrap180(a)
  a = (a + 180) % 360
  return a - 180
end

local function dial(g, cx, cy, r, rec)
  local base = "/nav/nav[" .. rec .. "]/"
  g:colour(120, 200, 255)
  g:text(cx, cy - r - 34, string.format("NAV%d  %.2f", rec + 1, n(base .. "frequency")), 1)
  g:colour(80, 110, 200)
  g:circle(cx, cy, r)
  for k = -5, 5 do
    if k ~= 0 then g:disc(cx + k * (r / 6), cy, 2) end
  end
  if not props.get(base .. "valid") then
    g:colour(150, 150, 150)
    g:text(cx, cy + r + 14, "no signal", 1)
    return
  end
  local kind = props.get(base .. "type")
  g:colour(120, 255, 140)
  g:text(cx, cy - r - 18, props.get(base .. "name") or "", 1)
  g:text(cx, cy + r + 14, string.format("%.1f km", n(base .. "distance") / 1000), 1)
  local course, dev, per, flag = n(base .. "obs"), 0, 10, ""
  if kind == "ils" and props.get(base .. "ils/valid") then
    course = n(base .. "ils/course")
    dev, per, flag = n(base .. "ils/localizer"), 2.5, "ILS"
    local gs = n(base .. "ils/glideslope")
    g:colour(255, 100, 255)
    local gy = cy + math.max(-r, math.min(r, gs / 2.5 * r))
    g:line(cx - r - 6, gy, cx - r + 6, gy)
  elseif kind == "vor" then
    local radial = n(base .. "radial")
    if math.abs(wrap180(radial - course)) < 90 then
      dev, flag = -wrap180(radial - course), "FR"
    else
      dev, flag = wrap180(radial - (course + 180)), "TO"
    end
  else
    g:colour(255, 200, 80)
    g:text(cx, cy, string.upper(kind or ""), 1)
    return
  end
  local off = math.max(-r, math.min(r, dev / per * r))
  g:colour(255, 255, 120)
  g:line(cx + off, cy - r + 10, cx + off, cy + r - 10)
  g:colour(255, 120, 255)
  g:tri(cx, cy - r - 2, cx - 6, cy - r + 10, cx + 6, cy - r + 10)
  g:colour(200, 210, 220)
  g:text(cx, cy, string.format("%03d", math.floor(course + 0.5) % 360), 1)
  g:text(cx, cy + 16, flag, 1)
end

return {
  name = "VOR/ILS",
  buttons = { "OBS1+", "OBS1-", "OBS2+", "OBS2-", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "VOR / ILS", 1)
    dial(g, 105, 190, 70, 0)
    dial(g, 295, 190, 70, 1)
    g:colour(120, 255, 140)
    for rec = 0, 1 do
      local base = "/nav/nav[" .. rec .. "]/"
      if props.get(base .. "valid") then
        g:text(40 + rec * 190, 330, string.format("Brg %03d", math.floor(n(base .. "bearing") + 0.5) % 360), 0)
        g:text(40 + rec * 190, 346, string.format("Rad %03d", math.floor(n(base .. "radial") + 0.5) % 360), 0)
        g:text(40 + rec * 190, 362, string.format("Cls %+.0f m/s", -n(base .. "closing-speed")), 0)
      end
    end
  end,

  button = function(self, i)
    local rec = (i == 1 or i == 2) and 0 or 1
    if i > 4 then return end
    local path = "/nav/nav[" .. rec .. "]/obs"
    local obs = n(path)
    props.set(path, (obs + ((i % 2 == 1) and 1 or -1)) % 360)
  end,
}
