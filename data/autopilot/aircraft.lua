-- Default aircraft autopilot. Engage with [P] or the Autopilot > Aircraft window. Each mode below is switched on or off
-- under /autopilot/ac/ and the targets are captured from the aircraft when the autopilot engages:
--   hdg-hold   hold /autopilot/ac/heading (degrees) by banking, at most 25 degrees
--   alt-hold   hold /autopilot/ac/altitude (metres), climbing or descending at most 5 m/s
--   vs-hold    hold /autopilot/ac/vs (m/s) instead of an altitude
--   spd-hold   autothrottle: hold /autopilot/ac/speed (m/s)
--   nav        steer onto the localizer of ILS receiver /autopilot/ac/receiver
--   appr       nav, and also follow that ILS's glideslope
-- Moving the pitch, yaw or roll controls, or pressing P again, hands the aircraft back to the pilot.

local ac = "/autopilot/ac/"
local engaged_for = 0
local held = false
local trim = 0
local throttle_hold = 0
local last_vmode = ""

local function wrap180(a)
  a = (a + 180) % 360
  return a - 180
end

local function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
local function flag(name) return props.get(ac .. name) end
local function num(path) return props.get(path) or 0 end

function update(dt)
  if not props.get("/autopilot/enabled") then
    held = false
    engaged_for = 0
    return
  end
  local ship = "/ships/ship[" .. num("/sim/active-ship") .. "]/"
  if props.get(ship .. "landed") then
    props.set("/autopilot/status", "Autopilot: on the ground")
    held = false
    return
  end
  engaged_for = engaged_for + dt
  if engaged_for < 0.3 then return end -- the ship's state is not valid in the first frames after a scenario loads
  local heading = num(ship .. "heading")
  local altitude = num(ship .. "altitude")
  local speed = num(ship .. "speed")
  local vertical = num(ship .. "vertical-speed")
  if not held then
    held = true
    props.set(ac .. "heading", heading)
    props.set(ac .. "altitude", altitude)
    props.set(ac .. "vs", 0.0)
    props.set(ac .. "speed", speed)
    throttle_hold = num("/controls/throttle")
    trim = 0
  end

  local parts = {}
  local receiver = "/nav/nav[" .. num(ac .. "receiver") .. "]/ils/"
  local ils = props.get(receiver .. "valid")
  local nav = (flag("nav") or flag("appr")) and ils

  -- Lateral: a bank angle (at most 25 degrees) that turns toward the wanted heading; the ailerons hold that bank.
  local wanted_heading
  if nav then
    wanted_heading = num(receiver .. "course") + clamp(num(receiver .. "localizer") * 4.0, -30, 30)
    parts[#parts + 1] = "LOC"
  elseif flag("hdg-hold") then
    wanted_heading = num(ac .. "heading")
    parts[#parts + 1] = string.format("HDG %03.0f", wanted_heading)
  end
  if wanted_heading then
    local bank = clamp(wrap180(wanted_heading - heading) * 1.2, -25, 25)
    props.set("/controls/roll", clamp((num(ship .. "roll") - bank) * 0.04, -0.5, 0.5))
  end

  -- Vertical: a wanted climb rate; the elevator holds it, with a slow trim for the steady-state error.
  local wanted_vs, vmode
  if flag("appr") and ils then
    local glide = math.rad(num(receiver .. "glide-angle"))
    wanted_vs = clamp(-speed * math.tan(glide) - num(receiver .. "glideslope") * 2.0, -8, 4)
    vmode = "GS"
    parts[#parts + 1] = "GS"
  elseif flag("alt-hold") then
    local target = num(ac .. "altitude")
    wanted_vs = clamp((target - altitude) * 0.05, -5, 5)
    vmode = "ALT"
    parts[#parts + 1] = string.format("ALT %.0f m", target)
  elseif flag("vs-hold") then
    wanted_vs = num(ac .. "vs")
    vmode = "VS"
    parts[#parts + 1] = string.format("VS %+.1f m/s", wanted_vs)
  end
  if vmode ~= last_vmode then
    trim = 0
    last_vmode = vmode or ""
  end
  if wanted_vs then
    local err = wanted_vs - vertical
    trim = clamp(trim + err * 0.004 * dt, -0.2, 0.2)
    props.set("/controls/pitch", clamp(err * 0.02 + trim, -0.4, 0.4))
  end

  -- Autothrottle.
  if flag("spd-hold") then
    local err = num(ac .. "speed") - speed
    throttle_hold = clamp(throttle_hold + err * 0.004 * dt, 0.0, 1.0)
    props.set("/controls/throttle", clamp(throttle_hold + err * 0.02, 0.0, 1.0))
    parts[#parts + 1] = string.format("SPD %.0f kt", num(ac .. "speed") * 1.94384)
  else
    throttle_hold = num("/controls/throttle")
  end

  props.set("/autopilot/status", "Autopilot: " .. (#parts > 0 and table.concat(parts, "  ") or "stand-by"))
end
