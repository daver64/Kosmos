-- Ascent Profile MFD: altitude, speed and pitch against time since the MFD was opened or reset.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local MAX = 300

return {
  name = "Ascent Profile",
  buttons = { "", "", "", "", "", "", "", "", "", "", "", "RESET" },

  init = function(self)
    self.t = {}
    self.timer = 0
  end,

  update = function(self, dt)
    self.timer = self.timer - dt
    if self.timer <= 0 then
      self.timer = 2
      local t = self.t
      t[#t + 1] = { ship("altitude"), ship("speed"), ship("pitch") }
      if #t > MAX then table.remove(t, 1) end
    end
  end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "ASCENT PROFILE", 1)
    local x0, y0, pw, ph = 50, 50, 300, 300
    g:colour(60, 120, 80)
    g:rect(x0, y0, x0 + pw, y0 + ph)
    for i = 1, 3 do
      g:line(x0, y0 + ph * i / 4, x0 + pw, y0 + ph * i / 4)
    end
    local n = #self.t
    local maxalt, maxspd = 1000, 100
    for _, p in ipairs(self.t) do
      maxalt = math.max(maxalt, p[1])
      maxspd = math.max(maxspd, p[2])
    end
    local function trace(idx, scale, r, gr, b)
      g:colour(r, gr, b)
      for i = 2, n do
        local a, c = self.t[i - 1], self.t[i]
        local x1 = x0 + (i - 1) / MAX * pw
        local x2 = x0 + i / MAX * pw
        g:line(x1, y0 + ph - math.max(0, a[idx]) / scale * ph, x2, y0 + ph - math.max(0, c[idx]) / scale * ph)
      end
    end
    trace(1, maxalt, 120, 255, 140)
    trace(2, maxspd, 255, 200, 80)
    trace(3, 90, 120, 180, 255)
    g:colour(120, 255, 140)
    g:text(x0, y0 + ph + 14, string.format("ALT %.0f km", maxalt / 1000), 0)
    g:colour(255, 200, 80)
    g:text(200, y0 + ph + 14, string.format("SPD %.0f m/s", maxspd), 1)
    g:colour(120, 180, 255)
    g:text(x0 + pw, y0 + ph + 14, "PITCH 90", 2)
  end,

  button = function(self, i)
    if i == 12 then
      self.t = {}
    end
  end,
}
