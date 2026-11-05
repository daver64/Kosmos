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

local function ship_path() return "/ships/ship[" .. props.get("/sim/active-ship") .. "]/systems/" end

-- Switchable loads (instruments, heaters, computers), in the ship's order, as component indices.
local function load_list()
  local list = {}
  for i = 0, num("component-count") - 1 do
    local c = "component[" .. i .. "]/"
    if sys(c .. "type") == "load" and num(c .. "watts") > 0 and num(c .. "failed") == 0 then list[#list + 1] = i end
  end
  return list
end

local function set_load(i, on) props.set(ship_path() .. "component[" .. i .. "]/enabled", on and 1 or 0) end

-- How long the battery lasts at the present net drain.
local function endurance()
  local net = num("bus/demand-w") - num("bus/supply-w")
  local wh = num("battery/wh")
  if wh <= 0 or num("battery/percent") <= 0 then return nil end
  if net <= 0.01 then return "BATTERY HOLDING" end
  local hours = wh / net
  if hours < 48 then return string.format("BATTERY LASTS %.1f h", hours) end
  return string.format("BATTERY LASTS %.1f d", hours / 24)
end

local tanks = { { "oxygen", "O2", 30 }, { "nitrogen", "N2", 80 }, { "water", "H2O", 200 }, { "hydrogen", "H2", 10 } }

local M
M = {
  name = "Systems",
  buttons = { "LEAK5", "LOADS", "", "", "", "", "", "", "", "", "", "" },
  draw = function(self, g, w, h)
    local forced = props.get("/systems-mfd/page")
    if forced then self.page = tonumber(forced) or self.page end
    M.set_buttons(self)
    if self.page == 2 then return M.draw_loads(self, g, w, h) end
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
    local left = endurance()
    if left then
      g:colour(200, 210, 220)
      g:text(64, y + 6, left, 0)
      y = y + 18
    end
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
  draw_loads = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(w / 2, 14, "SHIP LOADS", 1)
    if not sys("present") then
      g:colour(150, 150, 150)
      g:text(w / 2, h / 2, "no systems on this ship", 1)
      return
    end
    g:colour(200, 210, 220)
    g:text(64, 36, string.format("Supply %.0f W  Demand %.0f W", num("bus/supply-w"), num("bus/demand-w")), 0)
    bar(g, 100, 46, 150, num("battery/percent") / 100, "BATT", string.format("%.0f%%", num("battery/percent")))
    local left = endurance()
    if left then
      g:colour(200, 210, 220)
      g:text(64, 78, left, 0)
    end
    local list = load_list()
    if #list == 0 then
      g:colour(150, 150, 150)
      g:text(w / 2, h / 2, "no switchable loads", 1)
      return
    end
    self.cur = math.max(1, math.min(self.cur, #list))
    local rows = 12
    local first = math.max(1, math.min(self.cur - 5, #list - rows + 1))
    local y = 98
    for n = first, math.min(#list, first + rows - 1) do
      local c = "component[" .. list[n] .. "]/"
      local on = num(c .. "enabled") > 0.5
      local state = not on and "OFF" or (num(c .. "powered") > 0.5 and "ON" or "SHED")
      if state == "ON" then g:colour(120, 255, 140) elseif state == "OFF" then g:colour(140, 140, 150) else g:colour(255, 80, 80) end
      if n == self.cur then g:text(52, y, ">", 0) end
      g:text(64, y, string.format("%-24.24s %5.1f W  %s", sys(c .. "name") or "", num(c .. "watts"), state), 0)
      y = y + 16
    end
    g:colour(150, 160, 170)
    g:text(w / 2, h - 12, string.format("%d / %d", self.cur, #list), 1)
  end,
  set_buttons = function(self)
    local b = self.page == 2 and { "SYS", "UP", "DOWN", "TOGGLE", "SHED", "ALL ON" } or { "LEAK5", "LOADS", "", "", "", "" }
    for i = 1, 6 do M.buttons[i] = b[i] end
  end,
  init = function(self)
    self.max = {}
    self.page, self.cur = 1, 1
  end,
  button = function(self, i)
    if self.page == 2 then
      local list = load_list()
      if i == 1 then self.page = 1
      elseif i == 2 then self.cur = math.max(1, self.cur - 1)
      elseif i == 3 then self.cur = math.min(math.max(#list, 1), self.cur + 1)
      elseif i == 4 and list[self.cur] then
        set_load(list[self.cur], num("component[" .. list[self.cur] .. "]/enabled") <= 0.5)
      elseif i == 5 or i == 6 then
        -- SHED keeps only the essential (priority 0) loads; ALL ON restores everything.
        for _, idx in ipairs(list) do
          set_load(idx, i == 6 or num("component[" .. idx .. "]/priority") < 1)
        end
      end
      return
    end
    if i == 2 then self.page = 2; return end
    if i == 1 and num("vessel-count") > 0 then
      props.set("/ships/ship[" .. props.get("/sim/active-ship") .. "]/systems/vessel[0]/breach-cm2", 5)
    end
  end,
}

return M
