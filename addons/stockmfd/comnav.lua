-- COM/NAV MFD: the transmitters in range, and the two NAV receivers' frequencies.

local function n(path) return props.get(path) or 0 end

local function freq(f)
  if f < 1.0 then return string.format("%.0f kHz", f * 1000) end
  return string.format("%.2f", f)
end

local labels = { "NAV1", "NAV2", "F+", "F-", "F++", "F--", "1", "2", "3", "4", "5", "6" }

local mode
mode = {
  name = "COM/NAV",
  buttons = labels,
  init = function(self)
    self.rec = 0
    self.top = 0
  end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "COM / NAV", 1)
    for r = 0, 1 do
      local path = "/nav/nav[" .. r .. "]/frequency"
      local valid = props.get("/nav/nav[" .. r .. "]/valid")
      if r == self.rec then g:colour(255, 255, 255) else g:colour(120, 200, 255) end
      g:text(60, 40 + r * 18, string.format("NAV%d %s", r + 1, n(path) < 1.0 and freq(n(path)) or freq(n(path)) .. " MHz"), 0)
      if valid then
        g:colour(120, 255, 140)
        g:text(250, 40 + r * 18, (props.get("/nav/nav[" .. r .. "]/name") or ""), 0)
      else
        g:colour(150, 150, 150)
        g:text(250, 40 + r * 18, "no signal", 0)
      end
    end
    g:colour(60, 110, 255)
    g:line(60, 86, 340, 86)
    g:colour(200, 210, 220)
    g:text(60, 94, "Type", 0)
    g:text(110, 94, "Name", 0)
    g:text(330, 94, "MHz", 2)
    g:text(390, 94, "Range km", 2)
    local count = n("/nav/count")
    for k = 0, math.min(count, 6) - 1 do
      local base = "/nav/item[" .. k .. "]/"
      local y = 120 + k * 36
      g:colour(255, 240, 140)
      g:text(34, y, tostring(k + 1), 0)
      g:colour(120, 255, 140)
      g:text(60, y, string.upper(props.get(base .. "type") or ""), 0)
      g:text(110, y, props.get(base .. "name") or "", 0)
      g:colour(120, 200, 255)
      g:text(330, y, freq(n(base .. "frequency")), 2)
      g:text(390, y, string.format("%.1f", n(base .. "distance") / 1000), 2)
    end
    if count == 0 then
      g:colour(255, 200, 80)
      g:text(200, 200, "No transmitters in range", 1)
    end
  end,

  button = function(self, i)
    local path = "/nav/nav[" .. self.rec .. "]/frequency"
    if i == 1 then self.rec = 0
    elseif i == 2 then self.rec = 1
    elseif i == 3 then props.set(path, math.min(140.0, n(path) + 0.05))
    elseif i == 4 then props.set(path, math.max(0.0, n(path) - 0.05))
    elseif i == 5 then props.set(path, math.min(140.0, n(path) + 1.0))
    elseif i == 6 then props.set(path, math.max(0.0, n(path) - 1.0))
    elseif i >= 7 then
      local k = i - 7
      if k < n("/nav/count") then props.set(path, n("/nav/item[" .. k .. "]/frequency")) end
    end
  end,
}
return mode
