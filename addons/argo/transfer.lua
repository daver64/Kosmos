-- Argo trans-lunar autopilot. Like every addon script it only reads and writes the property tree.
--
-- Phases: phasing (time warp until the Moon is the right distance ahead), align, burn (trans-lunar injection), coast
-- (warp to the Moon's sphere of influence), capture (retrograde burn at periapsis), done.
-- Enable with [P] (or props.set('/autopilot/enabled', true)).

local MU_EARTH = 3.986004418e14
local R_EARTH = 6.371e6
-- Added to the ideal Moon lead angle (degrees). Tuned so the arrival passes the Moon at a safe altitude.
local LEAD_TRIM = 0.0
local CAPTURE_APOAPSIS = 4.0e6 -- metres of altitude above the Moon to settle into

local phase = "off"
local phase_time = 0.0
local earth, moon = -1, -1

local function set_phase(name, status)
  phase = name
  phase_time = 0.0
  props.set("/autopilot/status", status)
end

local function body_index(name)
  for i = 0, props.get("/universe/bodies/count") - 1 do
    if props.get("/universe/bodies/body[" .. i .. "]/name") == name then return i end
  end
  return -1
end

local function body_pos(i)
  local b = "/universe/bodies/body[" .. i .. "]/"
  return props.get(b .. "x"), props.get(b .. "y"), props.get(b .. "z")
end

local function ship_path() return "/ships/ship[" .. props.get("/sim/active-ship") .. "]" end
local function get(name) return props.get(ship_path() .. "/" .. name) end

local function warp(index) props.set("/sim/warp-index", index) end

-- Angle of a point about the Earth in the orbital plane, increasing in the direction a prograde ship moves.
local function angle(x, y, z) return math.atan(-z, x) end

local function wrap(a)
  a = a % (2 * math.pi)
  return a
end

-- Geometry of the transfer: lead angle of the Moon at the burn, and the burn's delta-v.
local function transfer(r1, r2)
  local a = (r1 + r2) / 2
  local flight_time = math.pi * math.sqrt(a ^ 3 / MU_EARTH)
  local moon_rate = math.sqrt(MU_EARTH / r2 ^ 3)
  local lead = math.pi - moon_rate * flight_time
  local dv = math.sqrt(MU_EARTH * (2 / r1 - 1 / a)) - math.sqrt(MU_EARTH / r1)
  return lead, dv
end

function update(dt)
  if not props.get("/autopilot/enabled") then
    phase = "off"
    return
  end
  if phase == "off" then
    earth = body_index("Earth")
    moon = body_index("Moon")
    set_phase("phasing", "Waiting for the transfer window")
  end
  phase_time = phase_time + dt

  local soi = props.get(ship_path() .. "/soi")
  local ex, ey, ez = body_pos(earth)

  if phase == "phasing" then
    local sx, sy, sz = get("x") - ex, get("y") - ey, get("z") - ez
    local mx, my, mz = body_pos(moon)
    mx, my, mz = mx - ex, my - ey, mz - ez
    local r1 = math.sqrt(sx * sx + sy * sy + sz * sz)
    local r2 = math.sqrt(mx * mx + my * my + mz * mz)
    local lead, dv = transfer(r1, r2)
    local burn_time = dv / math.max(get("twr") * 9.81, 0.1)
    local ship_rate = math.sqrt(MU_EARTH / r1 ^ 3)
    -- Start the burn early by half its arc so it is centred on the ideal point.
    local wanted = lead + math.rad(LEAD_TRIM) + ship_rate * burn_time * 0.5
    local error = wrap(angle(mx, my, mz) - angle(sx, sy, sz) - wanted)
    props.set("/controls/throttle", 0.0)
    props.set("/controls/sas", "prograde")
    if error < math.rad(6.0) or error > math.rad(354.0) then
      warp(0)
      if error < math.rad(0.3) or error > math.rad(359.7) then
        set_phase("burn", "Trans-lunar injection")
      end
    elseif error < math.rad(25.0) then
      warp(5)
    else
      warp(7)
    end

  elseif phase == "burn" then
    props.set("/controls/sas", "prograde")
    props.set("/controls/throttle", 1.0)
    local mx, my, mz = body_pos(moon)
    local target = math.sqrt((mx - ex) ^ 2 + (my - ey) ^ 2 + (mz - ez) ^ 2) - R_EARTH
    if get("orbit/apoapsis") >= target or get("stage-fuel") < 1.0 then
      props.set("/controls/throttle", 0.0)
      set_phase("coast", "Coasting to the Moon")
    end

  elseif phase == "coast" then
    props.set("/controls/throttle", 0.0)
    props.set("/controls/sas", "retrograde")
    if soi == "Moon" then
      warp(0)
      set_phase("capture", "Approaching the Moon")
    else
      local mx, my, mz = body_pos(moon)
      local sx, sy, sz = get("x"), get("y"), get("z")
      local distance = math.sqrt((mx - sx) ^ 2 + (my - sy) ^ 2 + (mz - sz) ^ 2)
      warp(distance > 2.0e8 and 9 or distance > 1.2e8 and 8 or 7)
    end

  elseif phase == "capture" then
    props.set("/controls/sas", "retrograde")
    local tb = get("orbit/time-to-periapsis")
    local vs = get("vertical-speed")
    if not get("orbit/bound") then
      -- Hyperbolic: estimate from the remaining descent to periapsis.
      if vs < -1.0 then tb = (get("altitude") - get("orbit/periapsis")) / -vs else tb = 0.0 end
    end
    if get("orbit/bound") and get("orbit/apoapsis") < math.max(CAPTURE_APOAPSIS, get("orbit/periapsis") + 1.5e6) then
      props.set("/controls/throttle", 0.0)
      props.set("/controls/sas", "stability")
      props.set("/autopilot/enabled", false)
      set_phase("off", "In lunar orbit")
    elseif tb < 60.0 or get("orbit/bound") then
      warp(0)
      props.set("/controls/throttle", 1.0)
    else
      props.set("/controls/throttle", 0.0)
      warp(tb > 20000.0 and 8 or tb > 2000.0 and 6 or tb > 300.0 and 4 or 0)
    end
  end
end
