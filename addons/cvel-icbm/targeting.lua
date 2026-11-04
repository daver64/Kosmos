-- ICBM Targeting MFD: target latitude and longitude, air or ground burst, burst height and yield (20 kt to 1 Mt), then ARM and LAUNCH.
-- The values live in /icbm/target/* and /controls/ordnance/*, so a script or --set can fill them in too.

local MAX_RANGE_KM = 4900
local MIN_RANGE_KM = 300
local YIELDS = { 20, 50, 100, 200, 300, 500, 750, 1000 }
local STEPS = { 0.01, 0.1, 1.0, 10.0 }

local function num(path, default)
  local v = props.get(path)
  if v == nil or v == "" then return default end
  return v
end

local function ensure()
  if props.get("/icbm/target/lat") == nil then props.set("/icbm/target/lat", 36.0) end
  if props.get("/icbm/target/lon") == nil then props.set("/icbm/target/lon", -55.0) end
  if num("/controls/ordnance/yield-kt", 0) <= 0 then props.set("/controls/ordnance/yield-kt", 20.0) end
  if num("/controls/ordnance/mode", "") == "" then props.set("/controls/ordnance/mode", "air") end
  if props.get("/controls/ordnance/burst-height") == nil then props.set("/controls/ordnance/burst-height", 500.0) end
end

local function yield_index()
  local y, best = num("/controls/ordnance/yield-kt", 20), 1
  for i, v in ipairs(YIELDS) do
    if math.abs(v - y) < math.abs(YIELDS[best] - y) then best = i end
  end
  return best
end

local function wrap_lon(v) return (v + 540.0) % 360.0 - 180.0 end

local function in_range()
  local r = num("/icbm/range-km", 0)
  return r >= MIN_RANGE_KM and r <= MAX_RANGE_KM
end

return {
  name = "ICBM Targeting",
  buttons = { "LAT+", "LAT-", "LON+", "LON-", "STEP", "BURST", "YIELD+", "YIELD-", "HGT+", "HGT-", "ARM", "LAUNCH" },

  init = function(self)
    self.step = 3
    ensure()
  end,

  draw = function(self, g, w, h)
    local air = num("/controls/ordnance/mode", "air") == "air"
    local armed = props.get("/icbm/armed") and true or false
    local range = num("/icbm/range-km", 0)
    local phase = num("/icbm/phase", "idle")
    g:colour(255, 120, 90)
    g:text(200, 14, "ICBM TARGETING", 1)
    g:colour(120, 255, 140)
    local x = 40
    local y = 50
    g:text(x, y, string.format("TARGET LAT  %+9.4f", num("/icbm/target/lat", 0)), 0)
    g:text(x, y + 22, string.format("TARGET LON  %+9.4f", num("/icbm/target/lon", 0)), 0)
    g:text(x, y + 44, string.format("STEP        %6.2f deg", STEPS[self.step]), 0)
    g:colour(255, 220, 120)
    g:text(x, y + 78, string.format("RANGE       %6.0f km  (%.0f mi)", range, range / 1.609), 0)
    if phase ~= "idle" then
      local tt = num("/icbm/time-to-impact", -1)
      g:colour(255, 160, 90)
      g:text(x, y + 100, tt >= 0 and string.format("TIME TO DET %d:%02d", tt // 60, math.floor(tt % 60)) or "TIME TO DET  (burning)", 0)
    elseif in_range() then
      g:colour(120, 255, 140)
      g:text(x, y + 100, "IN RANGE", 0)
    else
      g:colour(255, 90, 90)
      g:text(x, y + 100, range < MIN_RANGE_KM and "TOO CLOSE" or "OUT OF RANGE", 0)
    end
    g:colour(120, 200, 255)
    g:text(x, y + 134, string.format("BURST       %s", air and "AIR" or "GROUND"), 0)
    g:text(x, y + 156, string.format("HEIGHT      %6.0f m%s", num("/controls/ordnance/burst-height", 500), air and "" or "  (unused)"), 0)
    g:text(x, y + 178, string.format("YIELD       %6.0f kt", num("/controls/ordnance/yield-kt", 20)), 0)
    g:colour(255, 255, 255)
    g:text(x, y + 212, "STATUS  " .. num("/icbm/status", "Safe"), 0)
    if phase ~= "idle" then
      local miss = num("/icbm/miss-km", -1)
      g:text(x, y + 252, string.format("IMPACT  %+8.3f %+9.3f", num("/icbm/impact-lat", 0), num("/icbm/impact-lon", 0)), 0)
      g:text(x, y + 234, miss < 0 and "IMPACT PREDICTION  --" or string.format("PREDICTED MISS  %.1f km", miss), 0)
    end
    if armed then
      g:colour(255, 60, 60)
      g:text(200, 330, phase == "idle" and "ARMED" or "IN FLIGHT", 1)
    else
      g:colour(120, 140, 120)
      g:text(200, 330, "SAFE", 1)
    end
  end,

  button = function(self, i)
    if props.get("/icbm/armed") and num("/icbm/phase", "idle") ~= "idle" then return end
    local step = STEPS[self.step]
    if i == 1 then
      props.set("/icbm/target/lat", math.max(-90, math.min(90, num("/icbm/target/lat", 0) + step)))
    elseif i == 2 then
      props.set("/icbm/target/lat", math.max(-90, math.min(90, num("/icbm/target/lat", 0) - step)))
    elseif i == 3 then
      props.set("/icbm/target/lon", wrap_lon(num("/icbm/target/lon", 0) + step))
    elseif i == 4 then
      props.set("/icbm/target/lon", wrap_lon(num("/icbm/target/lon", 0) - step))
    elseif i == 5 then
      self.step = self.step % #STEPS + 1
    elseif i == 6 then
      props.set("/controls/ordnance/mode", num("/controls/ordnance/mode", "air") == "air" and "ground" or "air")
    elseif i == 7 then
      props.set("/controls/ordnance/yield-kt", YIELDS[math.min(#YIELDS, yield_index() + 1)])
    elseif i == 8 then
      props.set("/controls/ordnance/yield-kt", YIELDS[math.max(1, yield_index() - 1)])
    elseif i == 9 then
      props.set("/controls/ordnance/burst-height", math.min(10000, num("/controls/ordnance/burst-height", 500) + 100))
    elseif i == 10 then
      props.set("/controls/ordnance/burst-height", math.max(0, num("/controls/ordnance/burst-height", 500) - 100))
    elseif i == 11 then
      if props.get("/icbm/armed") then
        props.set("/icbm/armed", false)
        props.set("/icbm/status", "Safe")
      elseif in_range() then
        props.set("/icbm/armed", true)
        props.set("/icbm/status", "Armed - ready to launch")
      else
        props.set("/icbm/status", "Target out of range")
      end
    elseif i == 12 then
      if props.get("/icbm/armed") and in_range() then
        props.set("/icbm/launch", true)
      else
        props.set("/icbm/status", "Arm with a target in range first")
      end
    end
  end,
}
