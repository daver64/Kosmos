-- Instrument MFD: point a ship's cameras, radar and spectrometer, watch the live view, take shots and browse the gallery.

local function base() return "/ships/ship[" .. props.get("/sim/active-ship") .. "]/instruments/" end
local function inst(i, key) return props.get(base() .. "instrument[" .. i .. "]/" .. key) end
local function num(path, default) return tonumber(props.get(path)) or default or 0 end

local M = {
  name = "Instruments",
  buttons = { "INST", "SHOOT", "AZ-", "AZ+", "EL-", "EL+", "STEP", "GALLERY", "PREV", "NEXT", "", "" },
}

local function flag(path)
  local v = props.get(path)
  return v == true or v == "true" or (tonumber(v) or 0) > 0.5
end

local function count() return num(base() .. "count") end

local function selected()
  local n = count()
  if n == 0 then return nil end
  local s = math.floor(num("/sim/instrument/selected"))
  if s < 0 or s >= n then s = 0; props.set("/sim/instrument/selected", 0) end
  return s
end

local function nudge(i, daz, del)
  local max = num(base() .. "instrument[" .. i .. "]/max-slew", 180)
  local az = num(base() .. "instrument[" .. i .. "]/target-az") + daz
  local el = num(base() .. "instrument[" .. i .. "]/target-el") + del
  props.set(base() .. "instrument[" .. i .. "]/target-az", math.max(-max, math.min(max, az)))
  props.set(base() .. "instrument[" .. i .. "]/target-el", math.max(-max, math.min(max, el)))
end

local function draw_spectrum(g, x, y, w, h)
  g:colour(70, 90, 110)
  g:rect(x, y, x + w, y + h)
  local n = math.floor(num("/sim/instrument/spectrum/n"))
  if n < 2 then return end
  g:colour(120, 255, 140)
  local px, py
  for k = 0, n - 1 do
    local v = math.max(0, math.min(1, num("/sim/instrument/spectrum/v[" .. k .. "]")))
    local cx, cy = x + w * k / (n - 1), y + h - 2 - (h - 4) * v
    if px then g:line(px, py, cx, cy) end
    px, py = cx, cy
  end
  g:colour(150, 160, 170)
  g:text(x, y + h + 12, "0.3 um", 0)
  g:text(x + w, y + h + 12, "5.0 um", 2)
end

M.init = function(self)
  self.step = 5
  self.gallery = false
  self.gi = 0
end

M.draw = function(self, g, w, h)
  props.set("/sim/instrument/view-tick", num("/sim/instrument/view-tick") + 1)
  g:colour(255, 240, 140)
  g:text(w / 2, 20, "INSTRUMENTS ", 1)
  local i = selected()
  if not i then
    g:colour(150, 150, 150)
    g:text(w / 2, h / 2, "no instruments on this vessel", 1)
    return
  end

  if self.gallery then
    local n = math.floor(num("/sim/instrument/gallery/count"))
    g:colour(200, 210, 220)
    if n == 0 then
      g:text(w / 2, h / 2, "no saved shots", 1)
      return
    end
    self.gi = math.max(0, math.min(n - 1, self.gi))
    local file = props.get("/sim/instrument/gallery/file[" .. self.gi .. "]") or ""
    if file:sub(-4) == ".png" then
      g:image(file, w / 2, h / 2, 300, 300)
    else
      g:text(w / 2, h / 2, "spectrum data (csv)", 1)
    end
    g:colour(150, 160, 170)
    g:text(w / 2, h - 28, file:match("[^/]*$") or "", 1)
    g:text(w / 2, h - 12, string.format("%d / %d", self.gi + 1, n), 1)
    return
  end

  local type = inst(i, "type") or ""
  g:colour(200, 210, 220)
  g:text(64, 44, string.format("%d/%d %s", i + 1, count(), string.sub(inst(i, "name") or "", 1, 28)), 0)
  local powered = flag(base() .. "instrument[" .. i .. "]/powered")
  local shutter = flag(base() .. "instrument[" .. i .. "]/shutter")
  if not powered then
    g:colour(255, 80, 80)
    g:text(w - 64, 58, "NO POWER", 2)
  elseif shutter then
    g:colour(255, 160, 60)
    g:text(w - 64, 58, "SHUTTER: SUN", 2)
  else
    g:colour(120, 255, 140)
    g:text(w - 64, 58, "READY " .. type:upper(), 2)
  end

  local box = 260
  local cx, cy = w / 2, 66 + box / 2 + 6
  if type == "spectrometer" then
    draw_spectrum(g, cx - box / 2, 76, box, box - 60)
    g:colour(200, 210, 220)
    g:text(64, 76 + box - 40, "Species  " .. (props.get("/sim/instrument/species") or ""), 0)
  else
    g:colour(70, 90, 110)
    g:rect(cx - box / 2 - 1, cy - box / 2 - 1, cx + box / 2 + 1, cy + box / 2 + 1)
    if powered and not shutter then g:image("live:instrument", cx, cy, box, box) end
  end

  local y = cy + box / 2 + 20
  g:colour(200, 210, 220)
  g:text(64, y, string.format("Az %+.1f  El %+.1f   FOV %.1f", num(base() .. "instrument[" .. i .. "]/az"), num(base() .. "instrument[" .. i .. "]/el"), num(base() .. "instrument[" .. i .. "]/fov")), 0)
  g:text(64, y + 16, string.format("Target %s  %.0f km   Frames %d  Shots %d", props.get("/sim/instrument/target-name") or "--", num("/sim/instrument/target-range-km"), num(base() .. "instrument[" .. i .. "]/frames"), num(base() .. "instrument[" .. i .. "]/shots")), 0)
  g:text(64, y + 32, string.format("Step %d deg   Sun %.0f deg", self.step, num(base() .. "instrument[" .. i .. "]/sun-angle")), 0)
end

M.button = function(self, b)
  local i = selected()
  if not i then return end
  if b == 8 then self.gallery = not self.gallery; return end
  if self.gallery then
    if b == 9 then self.gi = self.gi - 1 elseif b == 10 then self.gi = self.gi + 1 end
    return
  end
  if b == 1 then
    props.set("/sim/instrument/selected", (i + 1) % count())
  elseif b == 2 then
    props.set(base() .. "instrument[" .. i .. "]/shoot", 1)
  elseif b == 3 then nudge(i, -self.step, 0)
  elseif b == 4 then nudge(i, self.step, 0)
  elseif b == 5 then nudge(i, 0, -self.step)
  elseif b == 6 then nudge(i, 0, self.step)
  elseif b == 7 then
    local steps = { 1, 5, 15, 45 }
    for k, s in ipairs(steps) do
      if s == self.step then self.step = steps[k % #steps + 1]; break end
    end
  end
end

return M
