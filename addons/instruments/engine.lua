-- Engine management: throttle, thrust, propellant, fuel flow and endurance for the active stage, plus the battery.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function num(path) return tonumber(ship(path)) or 0 end

local function bar(g, x, y, w, frac, label, value, r, gr, b)
  frac = math.max(0, math.min(1, frac))
  g:colour(70, 90, 110)
  g:rect(x, y, x + w, y + 16)
  g:colour(r or 120, gr or 255, b or 140)
  g:fillrect(x + 1, y + 1, x + 1 + (w - 2) * frac, y + 15)
  g:colour(200, 210, 220)
  g:text(x - 6, y + 9, label, 2)
  g:text(x + w + 8, y + 9, value, 0)
end

return {
  name = "Engine",
  buttons = {},
  init = function(self) self.peak = 0; self.flow = 0 end,

  update = function(self, dt)
    local fuel = num("stage-fuel")
    self.peak = math.max(self.peak or 0, fuel)
    if self.last and dt > 0 then
      local burn = math.max(0, (self.last - fuel) / dt)
      self.flow = self.flow + (burn - self.flow) * math.min(1, dt * 2)
    end
    self.last = fuel
  end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(w / 2, 20, "ENGINE MANAGEMENT", 1)
    local throttle = props.get("/controls/throttle") or 0
    local thrust, max = num("thrust"), num("max-thrust")
    local fuel, peak = num("stage-fuel"), math.max(self.peak or 0, 1)
    local flow = self.flow or 0
    bar(g, 100, 54, 190, throttle, "THR", string.format("%.0f%%", throttle * 100))
    bar(g, 100, 84, 190, max > 0 and thrust / max or 0, "THRUST", string.format("%.1f kN", thrust / 1000))
    bar(g, 100, 114, 190, fuel / peak, "FUEL", string.format("%.0f kg", fuel), fuel / peak < 0.15 and 255 or 120, fuel / peak < 0.15 and 90 or 255, 140)
    g:colour(200, 210, 220)
    g:text(64, 160, string.format("Flow       %.2f kg/s  (%.0f kg/h)", flow, flow * 3600), 0)
    local endurance = flow > 1e-4 and fuel / flow or nil
    g:text(64, 180, endurance and string.format("Endurance  %d min %02d s", endurance // 60, endurance % 60) or "Endurance  --", 0)
    g:text(64, 200, string.format("Max thrust %.1f kN   TWR %.2f", max / 1000, num("twr")), 0)
    g:text(64, 220, string.format("Total fuel %.0f kg   Mass %.0f kg", num("total-fuel"), num("mass")), 0)
    g:colour(255, 240, 140)
    g:text(w / 2, 262, "ELECTRICAL", 1)
    local sys = "/ships/ship[" .. props.get("/sim/active-ship") .. "]/systems/"
    if props.get(sys .. "present") then
      local pct = tonumber(props.get(sys .. "battery/percent")) or 0
      bar(g, 100, 280, 190, pct / 100, "BATT", string.format("%.0f%%", pct), pct < 20 and 255 or 120, pct < 20 and 90 or 255, 140)
      local ok = props.get(sys .. "bus/powered")
      g:colour(ok and 120 or 255, ok and 255 or 80, ok and 140 or 80)
      g:text(64, 316, ok and "BUS POWERED" or "BUS BROWN-OUT", 0)
      g:colour(200, 210, 220)
      g:text(64, 336, string.format("Supply %.0f W   Demand %.0f W", (tonumber(props.get(sys .. "bus/supply-w")) or 0), tonumber(props.get(sys .. "bus/demand-w")) or 0), 0)
    else
      g:colour(150, 150, 150)
      g:text(64, 290, "no systems on this ship", 0)
    end
  end,

  button = function(self, i) end,
}
