-- Fuel MFD: propellant of every stage, RCS propellant, flow and endurance, and fuel transfer with docked ships.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function num(path) return tonumber(ship(path)) or 0 end

local function bar(g, x, y, w, frac, label, value, active)
  frac = math.max(0, math.min(1, frac))
  g:colour(70, 90, 110)
  g:rect(x, y, x + w, y + 14)
  if frac < 0.15 then g:colour(255, 90, 90) elseif active then g:colour(120, 255, 140) else g:colour(100, 150, 120) end
  g:fillrect(x + 1, y + 1, x + 1 + (w - 2) * frac, y + 13)
  g:colour(active and 255 or 170, active and 255 or 180, active and 255 or 190)
  g:text(x - 6, y + 11, label, 2)
  g:colour(200, 210, 220)
  g:text(x + w + 8, y + 11, value, 0)
end

local modes = { "auto", "off", "give", "take" }

local function minutes(seconds)
  seconds = math.floor(seconds)
  if seconds >= 3600 then return string.format("%d h %02d min", seconds // 3600, (seconds % 3600) // 60) end
  return string.format("%d min %02d s", seconds // 60, seconds % 60)
end

return {
  name = "Fuel",
  buttons = { "AUTO", "OFF", "GIVE", "TAKE", "", "", "", "", "", "", "", "" },

  init = function(self) self.flow = 0 end,

  update = function(self, dt)
    local fuel = num("total-fuel") + num("rcs-fuel")
    if self.last and dt > 0 then
      local burn = math.max(0, (self.last - fuel) / dt)
      self.flow = self.flow + (burn - self.flow) * math.min(1, dt * 2)
    end
    self.last = fuel
  end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(w / 2, 20, "FUEL", 1)
    local y = 40
    local count, current = num("stage-count"), num("stage-index")
    for i = 0, math.min(count, 8) - 1 do
      local cap = num("stage[" .. i .. "]/capacity")
      if i >= current and cap > 0 then
        local kg = num("stage[" .. i .. "]/fuel")
        local name = ship("stage[" .. i .. "]/name") or ""
        bar(g, 120, y, 160, kg / cap, string.sub(name ~= "" and name or ("S" .. (i + 1)), 1, 7), string.format("%.0f kg", kg), i == current)
        y = y + 22
      end
    end
    local rcs_cap = num("rcs-fuel-capacity")
    if rcs_cap > 0 then
      bar(g, 120, y, 160, num("rcs-fuel") / rcs_cap, "RCS", string.format("%.1f kg", num("rcs-fuel")), true)
      y = y + 22
    end
    y = y + 10
    g:colour(200, 210, 220)
    local flow = self.flow or 0
    g:text(64, y, string.format("Total %.0f kg   Flow %.2f kg/s", num("total-fuel") + num("rcs-fuel"), flow), 0)
    local fuel = num("stage-fuel")
    g:text(64, y + 18, "Endurance  " .. (flow > 1e-4 and minutes(fuel / flow) or "--"), 0)

    y = y + 50
    g:colour(255, 240, 140)
    g:text(w / 2, y, "TRANSFER", 1)
    g:colour(200, 210, 220)
    if props.get("/dock/docked") then
      local mode = props.get("/dock/transfer") or "auto"
      g:text(64, y + 20, string.format("Docked ships hold %.0f of %.0f kg", tonumber(props.get("/dock/partner-fuel")) or 0, tonumber(props.get("/dock/partner-capacity")) or 0), 0)
      g:text(64, y + 38, "Mode  " .. mode:upper() .. "  (buttons at left)", 0)
    else
      g:colour(150, 150, 150)
      g:text(64, y + 20, "not docked", 0)
    end
  end,

  button = function(self, i)
    if i >= 1 and i <= #modes then props.set("/dock/transfer", modes[i]) end
  end,
}
