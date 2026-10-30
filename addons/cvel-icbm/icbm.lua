-- Guidance for the Sentinel ICBM. It flies a two-stage boost to a cut-off chosen so that the ballistic arc lands on the target
-- entered on the "ICBM Targeting" MFD, then opens the fairing, releases the warhead and backs the bus away with its retro motors.
-- Everything it knows comes from the property tree and everything it does is a property write.

local MU = 3.986004418e14
local OMEGA = 7.2921159e-5
local ship = "/ships/ship[0]"

local function atan2(y, x)
  if x > 0 then return math.atan(y / x) end
  if x < 0 then return math.atan(y / x) + (y >= 0 and math.pi or -math.pi) end
  return y >= 0 and math.pi / 2 or -math.pi / 2
end
local function get(name) return props.get(ship .. "/" .. name) or 0.0 end
local function rad(d) return d * math.pi / 180.0 end
local function deg(r) return r * 180.0 / math.pi end

-- Great-circle distance (m) and initial bearing (degrees) between two points given in degrees.
local function distance(lat1, lon1, lat2, lon2, radius)
  local p1, p2 = rad(lat1), rad(lat2)
  local dl = rad(lon2 - lon1)
  local a = math.sin((p2 - p1) / 2) ^ 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ^ 2
  return 2 * radius * math.asin(math.min(1.0, math.sqrt(a)))
end

local function bearing(lat1, lon1, lat2, lon2)
  local p1, p2 = rad(lat1), rad(lat2)
  local dl = rad(lon2 - lon1)
  local y = math.sin(dl) * math.cos(p2)
  local x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
  return (deg(atan2(y, x)) + 360.0) % 360.0
end

local function destination(lat1, lon1, az, angle)
  local p1, a = rad(lat1), rad(az)
  local p2 = math.asin(math.sin(p1) * math.cos(angle) + math.cos(p1) * math.sin(angle) * math.cos(a))
  local l2 = rad(lon1) + atan2(math.sin(a) * math.sin(angle) * math.cos(p1), math.cos(angle) - math.sin(p1) * math.sin(p2))
  return deg(p2), (deg(l2) + 540.0) % 360.0 - 180.0
end

-- Where the ship would land if the engines cut now (vacuum arc plus the rotating Earth), the flight time, and the inertial track azimuth.
local function predict()
  local radius = get("body-radius")
  local e = get("orbit/eccentricity")
  local a = get("orbit/semi-major-axis")
  local nu = rad(get("orbit/true-anomaly"))
  if e < 1e-4 or a <= 0 or e >= 1 then return nil end
  local p = a * (1 - e * e)
  local c = (p / radius - 1) / e
  if c > 1 or c < -1 then return nil end
  local nu_i = 2 * math.pi - math.acos(c)
  local dnu = (nu_i - nu) % (2 * math.pi)
  local function mean(v)
    local ea = 2 * math.atan(math.sqrt((1 - e) / (1 + e)) * math.tan(v / 2))
    return ea - e * math.sin(ea)
  end
  local dm = (mean(nu_i) - mean(nu)) % (2 * math.pi)
  local t = dm / math.sqrt(MU / (a * a * a))
  local lat, lon = get("latitude"), get("longitude")
  local horizontal = math.sqrt(math.max(get("speed") ^ 2 - get("vertical-speed") ^ 2, 0.0))
  local h = rad(get("heading"))
  local north = horizontal * math.cos(h)
  local east = horizontal * math.sin(h) + OMEGA * (radius + get("altitude")) * math.cos(rad(lat))
  local az = (deg(atan2(east, north)) + 360.0) % 360.0
  local plat, plon = destination(lat, lon, az, dnu)
  plon = (plon - deg(OMEGA * t) + 540.0) % 360.0 - 180.0
  return plat, plon, t, az, dnu * radius
end

local phase = "idle"
local phase_time = 0.0
local stage_sent = false
local pitch_cmd = 90.0
local trim = 0.0

local function status(text) props.set("/icbm/status", text) end

local function set_phase(name, text)
  phase = name
  phase_time = 0.0
  stage_sent = false
  props.set("/icbm/phase", name)
  status(text)
end

local function defaults()
  if props.get("/icbm/target/lat") == nil then props.set("/icbm/target/lat", 36.0) end
  if props.get("/icbm/target/lon") == nil then props.set("/icbm/target/lon", -55.0) end
  if props.get("/controls/ordnance/yield-kt") == nil or props.get("/controls/ordnance/yield-kt") <= 0 then props.set("/controls/ordnance/yield-kt", 20.0) end
  if props.get("/controls/ordnance/mode") == nil or props.get("/controls/ordnance/mode") == "" then props.set("/controls/ordnance/mode", "air") end
  if props.get("/controls/ordnance/burst-height") == nil then props.set("/controls/ordnance/burst-height", 500.0) end
  if props.get("/icbm/status") == nil or props.get("/icbm/status") == "" then status("Safe") end
end

local function steer(pitch, heading)
  props.set("/controls/sas", "attitude")
  props.set("/controls/target-pitch", pitch)
  props.set("/controls/target-heading", heading)
end

-- Flight-path angle the boost flies toward: steep off the pad, easing over to the cut-off angle by 90 km.
local function wanted_path(altitude, final)
  local f = math.max(0.0, 1.0 - altitude / 90000.0)
  return final + (88.0 - final) * f * f
end

function update(dt)
  defaults()
  local lat, lon = get("latitude"), get("longitude")
  local tlat, tlon = props.get("/icbm/target/lat") or 0.0, props.get("/icbm/target/lon") or 0.0
  local radius = math.max(get("body-radius"), 1.0)
  props.set("/icbm/range-km", distance(lat, lon, tlat, tlon, radius) / 1000.0)

  if phase == "idle" then
    if props.get("/icbm/launch") and props.get("/icbm/armed") then
      props.set("/icbm/launch", false)
      pitch_cmd = 90.0
      set_phase("liftoff", "Launch")
    else
      props.set("/icbm/launch", false)
      local r = props.get("/icbm/range-km") or 0.0
      if r < 300.0 or r > 4900.0 then
        props.set("/icbm/armed", false)
        status(r < 300.0 and "Target too close (min 300 km)" or "Target out of range (max 4900 km)")
      elseif not props.get("/icbm/armed") then
        status("Safe - target in range")
      end
    end
    return
  end

  phase_time = phase_time + dt
  local altitude = get("altitude")
  local vertical = get("vertical-speed")
  local stage = get("stage")
  local range_now = distance(lat, lon, tlat, tlon, radius)
  local central = range_now / radius
  local final = math.max(20.0, math.min(60.0, (180.0 - deg(central)) / 4.0 - 6.0))

  -- Aim the track at the target, which turns eastward with the Earth while the warhead is in flight.
  local est_time = 200.0 + range_now / 7000.0
  local brg = bearing(lat, lon, tlat, tlon + deg(OMEGA * est_time))

  if phase == "liftoff" then
    props.set("/controls/throttle", 1.0)
    steer(90.0, brg)
    if vertical > 60.0 then set_phase("boost", "Boost") end

  elseif phase == "boost" then
    props.set("/controls/throttle", 1.0)
    local path = get("flight-path")
    local target = wanted_path(altitude, final)
    if altitude < 800.0 then
      pitch_cmd = 90.0
    else
      pitch_cmd = math.max(final - 20.0, math.min(90.0, target + math.max(-25.0, math.min(25.0, (target - path) * 1.0))))
    end
    local _, _, _, az = predict()
    local lead = 0.0
    if az then lead = math.max(-12.0, math.min(12.0, ((brg - az + 540.0) % 360.0 - 180.0))) end
    steer(pitch_cmd, brg + lead + trim)
    status(stage == 0 and "Stage 1 burn" or "Stage 2 burn")
    if stage == 0 and get("stage-fuel") < 50.0 then
      set_phase("staging", "Staging")
    elseif stage >= 1 then
      local plat, plon, t, _, travel = predict()
      if plat then
        local miss = distance(plat, plon, tlat, tlon, radius)
        -- Steer the arc sideways until the predicted impact lies on the target's line.
        local rlon = tlon + deg(OMEGA * t)
        local d13 = distance(lat, lon, tlat, rlon, radius) / radius
        local th13 = rad(bearing(lat, lon, tlat, rlon))
        local cross = math.asin(math.max(-1.0, math.min(1.0, math.sin(d13) * math.sin(th13 - rad(az or brg))))) * radius / 1000.0
        trim = math.max(-8.0, math.min(8.0, trim + 0.003 * cross * dt))
        props.set("/icbm/miss-km", miss / 1000.0)
        local needed = distance(lat, lon, tlat, tlon + deg(OMEGA * t), radius)
        props.set("/icbm/predicted-range-km", travel / 1000.0)
        if travel >= needed and altitude > 60000.0 then
          props.set("/controls/throttle", 0.0)
          set_phase("cutoff", "Burnout")
        end
      end
      if get("stage-fuel") < 5.0 then
        props.set("/controls/throttle", 0.0)
        set_phase("cutoff", "Out of propellant")
      end
    end

  elseif phase == "staging" then
    props.set("/controls/throttle", 0.0)
    if phase_time > 0.6 and not stage_sent then
      stage_sent = true
      props.set("/controls/stage", true)
    end
    if stage_sent and phase_time > 2.5 then
      props.set("/controls/throttle", 1.0)
      set_phase("boost", "Stage 2 burn")
    end

  elseif phase == "cutoff" then
    props.set("/controls/throttle", 0.0)
    if phase_time > 1.0 and not stage_sent then
      stage_sent = true
      props.set("/controls/stage", true)
    end
    if stage_sent and phase_time > 4.0 then
      props.set("/controls/anim/fairing", 1.0)
      set_phase("fairing", "Fairing opening")
    end

  elseif phase == "fairing" then
    props.set("/controls/anim/fairing", 1.0)
    if get("anim/fairing") > 0.98 and phase_time > 7.0 then
      props.set("/controls/anim/warhead", 1.0)
      props.set("/controls/ordnance/release", true)
      set_phase("release", "Warhead away")
    end

  elseif phase == "release" then
    if phase_time > 8.0 then
      props.set("/controls/throttle", 0.12)
      set_phase("retro", "Retro burn")
    end

  elseif phase == "retro" then
    props.set("/controls/throttle", 0.12)
    if phase_time > 10.0 or get("stage-fuel") < 1.0 then
      props.set("/controls/throttle", 0.0)
      set_phase("done", "Warhead away - bus separated")
    end
  end
end
