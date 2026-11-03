-- Ascent autopilot shared by the CVEL launchers (a ship names it as "script": "@launcher.lua").
-- Everything it knows comes from the property tree and everything it does is a property write.
--
-- Enable with the [P] key or the Autopilot > Spacecraft window. It lifts off, pitches over, flies a programmed gravity turn,
-- stages each spent stage, then steers the last stage to the target apoapsis, coasts and circularises. The vehicle table
-- below gives each launcher's tuning; the Autopilot window sets the target orbit and the launch heading.

local ship = "/ships/ship[0]"
local MU_EARTH = 3.986004418e14
local R_EARTH = 6.371e6

-- kick_speed: vertical speed (m/s) at which the pitch kick starts. kick: pitch (degrees) after the kick.
-- closed_alt: altitude (m) where the programmed turn hands over to steering for the target orbit; the turn reaches mid_pitch there.
-- climb: vertical speed (m/s) the powered ascent steers for; a stage that cannot hold altitude does better with a low one.
-- lofted: above closed_alt steer from gravity minus centrifugal relief instead of chasing vertical speed, for stages with a thrust-to-weight
-- below 1; max_pitch caps how far it will pitch up to resist sinking.
-- inert_after, inert: after leaving that stage, this many engineless stages are jettisoned straight away.
-- last: the last stage the autopilot burns (a capsule or payload stage after it is left for the pilot).
local vehicles = {
  ["CVEL Soyuz-FG"] = { kick_speed = 35, kick = 88, closed_alt = 45000, mid_pitch = 35, climb = 250, gain = 0.2, last = 5, inert_after = 1, inert = 3 },
  ["CVEL Delta IV Heavy"] = { kick_speed = 40, kick = 86, closed_alt = 45000, mid_pitch = 35, climb = 150, last = 4, inert_after = 1, inert = 1 },
  ["CVEL Atlas III"] = { kick_speed = 40, kick = 86, closed_alt = 45000, mid_pitch = 35, climb = 150, last = 3, inert_after = 0, inert = 1 },
  ["CVEL Ariane 5 G"] = { kick_speed = 40, kick = 86, closed_alt = 45000, mid_pitch = 35, climb = 150, lofted = true, max_pitch = 40, last = 4, inert_after = 1, inert = 1 },
  ["CVEL Ariane 5 ES"] = { kick_speed = 40, kick = 86, closed_alt = 45000, mid_pitch = 35, climb = 150, lofted = true, max_pitch = 40, last = 4, inert_after = 1, inert = 1 },
  ["CVEL Ariane 5 ECA"] = { kick_speed = 40, kick = 86, closed_alt = 45000, mid_pitch = 35, climb = 150, lofted = true, max_pitch = 40, last = 4, inert_after = 1, inert = 1 },
  ["CVEL Energia"] = { kick_speed = 50, kick = 86, closed_alt = 55000, mid_pitch = 35, climb = 150, last = 1 },
  ["CVEL Energia-M"] = { kick_speed = 50, kick = 86, closed_alt = 55000, mid_pitch = 35, climb = 150, last = 1 },
  ["CVEL Saturn V"] = { kick_speed = 50, kick = 87, closed_alt = 55000, mid_pitch = 35, climb = 150, last = 2 },
  ["CVEL Saturn V MLV3"] = { kick_speed = 50, kick = 87, closed_alt = 55000, mid_pitch = 35, climb = 150, last = 2 },
  ["CVEL Saturn V-23L"] = { kick_speed = 50, kick = 87, closed_alt = 55000, mid_pitch = 35, climb = 150, last = 3 },
  ["CVEL Proton-K"] = { kick_speed = 40, kick = 86, closed_alt = 45000, mid_pitch = 35, climb = 150, last = 4, inert_after = 1, inert = 1 },
}
local default = { kick_speed = 50, kick = 85, closed_alt = 45000, mid_pitch = 35, climb = 150, last = 1 }
local PITCH_RATE = 2.0 -- degrees per second the commanded pitch may change

local phase = "off"
local phase_time = 0.0
local stage_sent = false
local presses = 0
local next_press = 0.0
local config = default
local pitch_cmd = 90.0

local function get(name) return props.get(ship .. "/" .. name) end

local function set_phase(name, status)
  phase = name
  phase_time = 0.0
  stage_sent = false
  props.set("/autopilot/status", status)
end

local function steer(pitch, dt)
  local step = PITCH_RATE * dt
  pitch_cmd = pitch_cmd + math.max(-step, math.min(step, pitch - pitch_cmd))
  props.set("/controls/target-pitch", pitch_cmd)
end

local function finish(status)
  props.set("/controls/throttle", 0.0)
  props.set("/autopilot/enabled", false)
  props.set("/controls/sas", "stability")
  set_phase("off", status)
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
    config = vehicles[get("class")] or default
    pitch_cmd = 90.0
    set_phase("liftoff", "Liftoff")
  end
  phase_time = phase_time + dt
  local target = (props.get("/autopilot/sc/target-altitude") or 200.0) * 1000.0
  local heading = props.get("/autopilot/sc/heading") or 90.0

  local altitude = get("altitude")
  local speed = get("speed")
  local vertical = get("vertical-speed")
  local apoapsis = get("orbit/apoapsis")
  local periapsis = get("orbit/periapsis")
  local stage = get("stage")

  if phase == "liftoff" then
    props.set("/controls/throttle", 1.0)
    props.set("/controls/sas", "attitude")
    props.set("/controls/target-pitch", 90.0)
    props.set("/controls/target-heading", heading)
    if vertical > config.kick_speed then
      set_phase("kick", "Pitch kick")
    end

  elseif phase == "kick" then
    props.set("/controls/target-heading", heading)
    pitch_cmd = config.kick
    props.set("/controls/target-pitch", pitch_cmd)
    local horizontal = math.sqrt(math.max(speed * speed - vertical * vertical, 0.0))
    local flight_path = math.deg(math.atan(vertical, horizontal))
    if flight_path < config.kick - 0.5 or phase_time > 15.0 then
      set_phase("gravity-turn", "Gravity turn")
    end

  elseif phase == "gravity-turn" then
    props.set("/controls/throttle", 1.0)
    props.set("/controls/sas", "attitude")
    props.set("/controls/target-heading", heading)
    local last = stage >= config.last
    if altitude < config.closed_alt then
      -- Programmed turn: pitch falls with the square root of altitude.
      local fraction = math.sqrt(math.max(altitude, 0.0) / config.closed_alt)
      steer(config.kick - (config.kick - config.mid_pitch) * fraction, dt)
      props.set("/autopilot/status", "Gravity turn")
    else
      -- Steer to hold a vertical speed that falls to zero as the orbit rises to the target altitude.
      local wanted = math.max(math.min((target - altitude) * 0.012, config.climb), -20.0)
      if config.lofted then
        local r = R_EARTH + altitude
        local horizontal = math.sqrt(math.max(speed * speed - vertical * vertical, 0.0))
        local up = MU_EARTH / (r * r) - horizontal * horizontal / r + 0.05 * (wanted - vertical)
        local accel = math.max(get("twr") * 9.81, 1.0)
        local pitch = math.deg(math.asin(math.max(math.min(up / accel, 1.0), -0.2)))
        steer(math.max(math.min(pitch, config.max_pitch), -10.0), dt)
      else
        steer(math.max(math.min((config.gain or 0.4) * (wanted - vertical), 85.0), -10.0), dt)
      end
      props.set("/autopilot/status", last and "Upper stage burn" or "Powered ascent")
      if last and apoapsis >= target then
        props.set("/controls/throttle", 0.0)
        set_phase("coast", "Coasting to apoapsis")
      end
    end
    if get("stage-fuel") < 50.0 then
      if stage < config.last then
        set_phase("staging", "Staging")
      elseif apoapsis < target then
        finish("Out of propellant")
      end
    end

  elseif phase == "staging" then
    props.set("/controls/throttle", 0.0)
    -- Stages that carry no engine (an escape tower, fairing halves) are jettisoned together with the staging.
    if phase_time > 0.6 and not stage_sent then
      stage_sent = true
      presses = 1 + ((config.inert_after == stage) and config.inert or 0)
      next_press = phase_time
    end
    if presses > 0 and phase_time >= next_press then
      presses = presses - 1
      next_press = phase_time + 0.4
      props.set("/controls/stage", true)
    end
    if stage_sent and presses == 0 and phase_time > next_press + 1.4 then
      props.set("/controls/throttle", 1.0)
      set_phase("gravity-turn", "Powered ascent")
    end

  elseif phase == "coast" then
    props.set("/controls/throttle", 0.0)
    props.set("/controls/sas", "prograde")
    local ra = R_EARTH + apoapsis
    local rp = R_EARTH + math.max(periapsis, 0.0)
    local dv = math.sqrt(MU_EARTH / ra) - apoapsis_speed(ra, rp)
    -- Half the burn before the apoapsis and half after, at the acceleration the remaining stage gives.
    local burn_time = dv / math.max(get("twr") * 9.81, 1.0)
    local lead = get("orbit/time-to-apoapsis") - burn_time * 0.5
    -- Leave warp well ahead of the burn: at high warp one frame skips many seconds.
    if lead < 300.0 and props.get("/sim/warp-index") > 0 then
      props.set("/sim/warp-index", 0)
      props.set("/autopilot/status", "Preparing burn")
    end
    if lead < 1.0 or vertical < -5.0 then
      props.set("/sim/warp-index", 0)
      set_phase("circularise", "Circularising")
    end

  elseif phase == "circularise" then
    props.set("/controls/sas", "prograde")
    props.set("/controls/throttle", 1.0)
    if periapsis >= target - 5000.0 or (periapsis > 100000.0 and apoapsis - periapsis < 15000.0) then
      finish("In orbit")
    elseif get("stage-fuel") < 1.0 then
      finish("Out of propellant")
    end
  end
end
