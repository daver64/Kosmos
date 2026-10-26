-- Systems MFD: power bus, battery, consumable tanks and the state of each pressure vessel.

local function sys(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/systems/" .. path)
end

local function num(path) return tonumber(sys(path)) or 0 end

local function bar(g, x, y, w, frac, label, value)
  frac = math.max(0, math.min(1, frac))
  g:colour(70, 90, 110)
  g:rect(x, y, x + w, y + 12)
  g:colour(120, 255, 140)
  g:fillrect(x + 1, y + 1, x + 1 + (w - 2) * frac, y + 11)
  g:colour(200, 210, 220)
  g:text(x - 6, y + 11, label, 2)
  g:text(x + w + 6, y + 11, value, 0)
end

local tanks = { { "oxygen", "O2", 30 }, { "nitrogen", "N2", 80 }, { "water", "H2O", 200 }, { "hydrogen", "H2", 10 } }

return {
  name = "Systems",
  buttons = { "LEAK5", "", "", "", "", "", "", "", "", "", "", "" },
  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(w / 2, 14, "SHIP SYSTEMS", 1)
    if not sys("present") then
      g:colour(150, 150, 150)
      g:text(w / 2, h / 2, "no systems on this ship", 1)
      return
    end
    local supply, demand = num("bus/supply-w"), num("bus/demand-w")
    g:colour(sys("bus/powered") and 120 or 255, sys("bus/powered") and 255 or 80, sys("bus/powered") and 140 or 80)
    g:text(64, 40, string.format("BUS  %s", sys("bus/powered") and "POWERED" or "BROWN-OUT"), 0)
    g:colour(200, 210, 220)
    g:text(64, 56, string.format("Supply %.0f W  Demand %.0f W", supply, demand), 0)
    bar(g, 100, 68, 150, num("battery/percent") / 100, "BATT", string.format("%.0f%%", num("battery/percent")))
    local y = 98
    for _, t in ipairs(tanks) do
      local kg = num("tank/" .. t[1] .. "-kg")
      if kg > 0 or t[1] == "oxygen" then
        bar(g, 100, y, 150, math.min(1, kg / (self.max[t[1]] or math.max(kg, 1))), t[2], string.format("%.1f kg", kg))
        self.max[t[1]] = math.max(self.max[t[1]] or 0, kg)
        y = y + 18
      end
    end
    y = y + 8
    for i = 0, num("vessel-count") - 1 do
      local v = "vessel[" .. i .. "]/"
      local hz = sys(v .. "hazard") or ""
      if hz ~= "" then g:colour(255, 80, 80) else g:colour(120, 255, 140) end
      g:text(64, y, string.format("%s  %s", sys(v .. "name") or "", hz ~= "" and hz:upper() or "NOMINAL"), 0)
      g:colour(200, 210, 220)
      g:text(64, y + 16, string.format("P %.1f kPa  O2 %.1f  CO2 %.2f", num(v .. "pressure-kpa"), num(v .. "ppo2-kpa"), num(v .. "ppco2-kpa")), 0)
      g:text(64, y + 32, string.format("Health %.0f%%  leak %.1e/s", num(v .. "health") * 100, num(v .. "leak-rate")), 0)
      y = y + 52
    end
    local alarm = sys("alarm") or ""
    if alarm ~= "" then
      g:colour(255, 80, 80)
      g:text(w / 2, h - 12, alarm, 1)
    end
  end,
  init = function(self) self.max = {} end,
  button = function(self, i)
    if i == 1 and num("vessel-count") > 0 then
      props.set("/ships/ship[" .. props.get("/sim/active-ship") .. "]/systems/vessel[0]/breach-cm2", 5)
    end
  end,
}
