-- ADF MFD: a needle that points at the NDB tuned on either NAV receiver, relative to the nose.
-- NDB frequencies are 190-535 kHz; use the buttons to step the receiver in 1 kHz and 10 kHz.

local function n(path) return props.get(path) or 0 end

local function needle(g, cx, cy, r, rec)
  local base = "/nav/nav[" .. rec .. "]/"
  local freq = n(base .. "frequency")
  g:colour(120, 200, 255)
  g:text(cx, cy - r - 34, string.format("ADF%d  %.0f kHz", rec + 1, freq * 1000), 1)
  g:colour(80, 110, 200)
  g:circle(cx, cy, r)
  for k = 0, 11 do
    local a = math.rad(k * 30)
    local inner = (k % 3 == 0) and 0.86 or 0.93
    g:line(cx + math.sin(a) * r * inner, cy - math.cos(a) * r * inner, cx + math.sin(a) * r, cy - math.cos(a) * r)
  end
  g:colour(200, 210, 220)
  g:text(cx, cy - r + 14, "0", 1)
  if not props.get(base .. "valid") or props.get(base .. "type") ~= "ndb" then
    g:colour(150, 150, 150)
    g:text(cx, cy + r + 14, props.get(base .. "valid") and "not an NDB" or "no signal", 1)
    return
  end
  g:colour(120, 255, 140)
  g:text(cx, cy - r - 18, props.get(base .. "ident") or "", 1)
  g:text(cx, cy + r + 14, string.format("%.1f km", n(base .. "distance") / 1000), 1)
  local a = math.rad(n(base .. "relative-bearing"))
  local tx, ty = cx + math.sin(a) * r * 0.8, cy - math.cos(a) * r * 0.8
  g:colour(255, 190, 60)
  g:line(cx, cy, tx, ty)
  local hx, hy = math.sin(a + 2.8) * 12, -math.cos(a + 2.8) * 12
  local kx, ky = math.sin(a - 2.8) * 12, -math.cos(a - 2.8) * 12
  g:line(tx, ty, tx + hx, ty + hy)
  g:line(tx, ty, tx + kx, ty + ky)
  g:colour(200, 210, 220)
  g:text(cx, cy + r + 30, string.format("Brg %03d", math.floor(n(base .. "bearing") + 0.5) % 360), 1)
end

return {
  name = "ADF",
  buttons = { "NAV1 +1", "NAV1 -1", "NAV1 +10", "NAV1 -10", "NAV2 +1", "NAV2 -1", "NAV2 +10", "NAV2 -10", "", "", "", "" },
  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(w / 2, 14, "ADF", 1)
    needle(g, w * 0.27, h * 0.5, 80, 0)
    needle(g, w * 0.73, h * 0.5, 80, 1)
  end,
  button = function(self, i)
    if i > 8 then return end
    local rec = (i <= 4) and 0 or 1
    local step = ((i - 1) % 4 < 2) and 0.001 or 0.01
    local sign = (i % 2 == 1) and 1 or -1
    local path = "/nav/nav[" .. rec .. "]/frequency"
    props.set(path, math.max(0.0, math.min(140.0, n(path) + sign * step)))
  end,
}
