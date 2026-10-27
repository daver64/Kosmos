-- Kestrel ascent autopilot. It is an ordinary addon script: everything it knows comes from the property tree and
-- everything it does is a property write, so it can be replaced or extended without touching the simulator.
--
-- Enable with the [P] key (or props.set('/autopilot/enabled', true)). Any manual throttle cut disables it.

local ship = "/ships/ship[0]"
local TARGET_ALTITUDE = 200000.0 -- metres, circular orbit; the Autopilot window changes it
local HEADING = 90.0
local MU_EARTH = 3.986004418e14
local R_EARTH = 6.371e6

local phase = "off"
local phase_time = 0.0

local function get(name) return props.get(ship .. "/" .. name) end

local stage_sent = false

local function set_phase(name, status)
  phase = name
  phase_time = 0.0
  stage_sent = false
  props.set("/autopilot/status", status)
end

-- Speed at the apoapsis of an orbit with the given apoapsis and periapsis radii.
local function apoapsis_speed(ra, rp)
  local a = (ra + rp) / 2
  return math.sqrt(MU_EARTH * (2 / ra - 1 / a))
end

function update(dt)
  if not props.get("/autopilot/enabled") then
    phase = "off"
    return
  end
  if phase == "off" then
    set_phase("liftoff", "Liftoff")
  end
  phase_time = phase_time + dt
  TARGET_ALTITUDE = (props.get("/autopilot/sc/target-altitude") or 200.0) * 1000.0
  HEADING = props.get("/autopilot/sc/heading") or 90.0

  local altitude = get("altitude")
  local speed = get("speed")
  local vertical = get("vertical-speed")
  local apoapsis = get("orbit/apoapsis")
  local periapsis = get("orbit/periapsis")

  if phase == "liftoff" then
    props.set("/controls/throttle", 1.0)
    props.set("/controls/sas", "attitude")
    props.set("/controls/target-pitch", 90.0)
    props.set("/controls/target-heading", HEADING)
    if vertical > 70.0 then
      set_phase("kick", "Pitch kick")
    end

  elseif phase == "kick" then
    props.set("/controls/target-pitch", 84.0)
    props.set("/controls/target-heading", HEADING)
    local horizontal = math.sqrt(math.max(speed * speed - vertical * vertical, 0.0))
    local flight_path = math.deg(math.atan(vertical, horizontal))
    if flight_path < 86.5 or phase_time > 12.0 then
      set_phase("gravity-turn", "Gravity turn")
    end

  elseif phase == "gravity-turn" then
    props.set("/controls/sas", "attitude")
    props.set("/controls/target-heading", HEADING)
    -- Programmed turn: pitch falls with the square root of altitude, reaching the horizon by about 110 km.
    props.set("/controls/target-pitch", math.max(3.0, 84.0 - 81.0 * math.sqrt(math.max(altitude, 0.0) / 110000.0)))
    if get("stage-fuel") < 50.0 and get("stage") == 0 then
      set_phase("staging", "Staging")
    end

  elseif phase == "staging" then
    props.set("/controls/throttle", 0.0)
    if phase_time > 0.6 and not stage_sent then
      stage_sent = true
      props.set("/controls/stage", true)
    end
    if phase_time > 2.0 then
      props.set("/controls/throttle", 1.0)
      set_phase("ascent", "Upper stage burn")
    end

  elseif phase == "ascent" then
    props.set("/controls/throttle", 1.0)
    props.set("/controls/sas", "attitude")
    props.set("/controls/target-heading", HEADING)
    -- Steer to hold a vertical speed that falls to zero as the apoapsis rises to the target altitude.
    local wanted = math.max(math.min((TARGET_ALTITUDE - altitude) * 0.012, 150.0), -20.0)
    local pitch = math.max(math.min(0.25 * (wanted - vertical), 40.0), -10.0)
    props.set("/controls/target-pitch", pitch)
    if apoapsis >= TARGET_ALTITUDE then
      props.set("/controls/throttle", 0.0)
      set_phase("coast", "Coasting to apoapsis")
    end

  elseif phase == "coast" then
    props.set("/controls/throttle", 0.0)
    props.set("/controls/sas", "prograde")
    local ra = R_EARTH + apoapsis
    local rp = R_EARTH + math.max(periapsis, 0.0)
    local dv = math.sqrt(MU_EARTH / ra) - apoapsis_speed(ra, rp)
    -- Half the burn before the apoapsis and half after, at about 1 g of acceleration from the remaining stage.
    local burn_time = dv / math.max(get("twr") * 9.81, 1.0)
    if get("orbit/time-to-apoapsis") < burn_time * 0.5 + 1.0 then
      set_phase("circularise", "Circularising")
    end

  elseif phase == "circularise" then
    props.set("/controls/sas", "prograde")
    props.set("/controls/throttle", 1.0)
    if periapsis >= TARGET_ALTITUDE - 5000.0 or get("stage-fuel") < 1.0 then
      props.set("/controls/throttle", 0.0)
      props.set("/autopilot/enabled", false)
      props.set("/controls/sas", "stability")
      set_phase("off", "In orbit")
    end
  end
end
