-- Transfer MFD: a Hohmann transfer plan from the ship's orbit to the target's, assuming both are near circular and coplanar.

local function target_list()
  local list = {}
  local active = props.get("/sim/active-ship")
  for i = 0, props.get("/ships/count") - 1 do
    if i ~= active then list[#list + 1] = props.get("/ships/ship[" .. i .. "]/name") end
  end
  for i = 0, props.get("/universe/bodies/count") - 1 do
    list[#list + 1] = props.get("/universe/bodies/body[" .. i .. "]/name")
  end
  return list
end

local function cycle_target(dir)
  local list = target_list()
  local cur, idx = props.get("/sim/target"), 0
  for i, n in ipairs(list) do if n == cur then idx = i end end
  idx = (idx - 1 + dir) % #list + 1
  props.set("/sim/target", list[idx])
end

local function fmt_time(s)
  if s == nil or s < 0 then return "--" end
  return string.format("%d:%02d:%02d", math.floor(s / 3600), math.floor((s % 3600) / 60), math.floor(s % 60))
end

local function minsec(seconds)
  seconds = math.floor(seconds + 0.5)
  return string.format("%d min %02d s", seconds // 60, seconds % 60)
end

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function soi_gm()
  local name = ship("soi")
  for i = 0, props.get("/universe/bodies/count") - 1 do
    if props.get("/universe/bodies/body[" .. i .. "]/name") == name then
      return props.get("/universe/bodies/body[" .. i .. "]/gm")
    end
  end
  return 0
end

-- The Hohmann plan from the current ship and target orbits, or nil when there is none.
local function plan()
  if not props.get("/target/valid") or not props.get("/target/in-plane-data") then return nil end
  local mu = soi_gm()
  local r1, r2 = props.get("/target/ship-radius"), props.get("/target/radius")
  local a = (r1 + r2) / 2
  local v1, v2 = math.sqrt(mu / r1), math.sqrt(mu / r2)
  local p = {
    r1 = r1, r2 = r2,
    dv1 = v1 * (math.sqrt(2 * r2 / (r1 + r2)) - 1),
    dv2 = v2 * (1 - math.sqrt(2 * r1 / (r1 + r2))),
    tof = math.pi * math.sqrt(a ^ 3 / mu),
  }
  local ns, nt = math.sqrt(mu / r1 ^ 3), math.sqrt(mu / r2 ^ 3)
  p.lead = math.pi - nt * p.tof
  p.phase = math.rad(props.get("/target/phase-angle"))
  if math.abs(ns - nt) > 1e-12 then
    p.synodic = 2 * math.pi / math.abs(ns - nt)
    p.wait = ((p.lead - p.phase) / (nt - ns)) % p.synodic
  end
  return p
end

-- Arm the burn autopilot for the next window (it only plans the raising burn; the pilot trims the approach).
local function arm()
  local p = plan()
  if not p or not p.wait or p.dv1 <= 0 then
    return
  end
  local accel = ship("max-thrust") / math.max(ship("mass"), 1)
  local dur = accel > 0 and math.abs(p.dv1) / accel or 0
  local wait = p.wait
  -- A window too close to leave time for the pre-burn turn waits for the next one.
  if wait < dur / 2 + 60 then wait = wait + p.synodic end
  props.set("/autopilot/burn/target-radius", p.r2)
  props.set("/autopilot/burn/start", props.get("/sim/time") + wait)
  props.set("/autopilot/burn/state", "wait")
  props.set("/autopilot/burn/armed", true)
end

local function bar(g, x, y, w, frac)
  g:colour(60, 110, 255)
  g:rect(x, y, x + w, y + 10)
  g:colour(255, 200, 80)
  if frac > 0.01 then g:fillrect(x + 1, y + 1, x + 1 + (w - 2) * math.min(frac, 1), y + 9) end
end

-- Plan diagram: the central body, your actual orbit (green) with a white dot where you are, the target's orbit
-- (orange) with its dot, and for a near circular orbit the planned transfer ellipse (yellow). A hollow ring marks where
-- the target must be when you burn. If you are already on an eccentric orbit (after burn 1) that ellipse is drawn instead
-- of a circle. When the sizes differ a lot the radii are compressed, so the picture is not to scale.
local function transfer_diagram(g, cx, cy, R, r1, r2, phase, lead, centre, from_name, to_name, so)
  local ecc = so and so.e or 0
  local p = so and so.a and so.a * (1 - ecc * ecc) or r1
  local actual = so and ecc > 0.02 and ecc < 0.98
  local ra, rp = r1, r1
  if actual then ra, rp = p / (1 - ecc), p / (1 + ecc) end
  local rmax = math.max(r1, r2, ra)
  local rmin = math.min(r1, r2, rp)
  local k = 1
  if rmin / rmax < 0.32 then k = math.min(1, math.log(0.32) / math.log(rmin / rmax)) end
  local function disp(r) return R * (r / rmax) ^ k end
  local d1, d2 = disp(r1), disp(r2)
  g:colour(90, 140, 255)
  g:disc(cx, cy, 4)
  g:text(cx, cy + 8, centre, 1)
  g:colour(255, 140, 60)
  g:circle(cx, cy, d2)
  local w = so and math.rad(-so.nu) or 0
  g:colour(120, 255, 140)
  if actual then
    local px, py
    for i = 0, 72 do
      local t = (2 * math.pi) * i / 72
      local d = disp(p / (1 + ecc * math.cos(t - w)))
      local x, y = cx + d * math.cos(t), cy - d * math.sin(t)
      if px then g:line(px, py, x, y) end
      px, py = x, y
    end
  else
    g:circle(cx, cy, d1)
    g:colour(255, 200, 80)
    local e, q
    if d2 >= d1 then e = (d2 - d1) / (d2 + d1); q = d1 * (1 + e) else e = (d1 - d2) / (d1 + d2); q = d2 * (1 + e) end
    local px, py
    for i = 0, 48 do
      local t = math.pi * i / 48
      local r = d2 >= d1 and q / (1 + e * math.cos(t)) or q / (1 - e * math.cos(t))
      local x, y = cx + r * math.cos(t), cy - r * math.sin(t)
      if px then g:line(px, py, x, y) end
      px, py = x, y
    end
  end
  g:colour(255, 140, 60)
  if not actual then g:circle(cx + d2 * math.cos(lead), cy - d2 * math.sin(lead), 5) end
  g:disc(cx + d2 * math.cos(phase), cy - d2 * math.sin(phase), 4)
  g:text(cx + d2 * math.cos(phase) + 7, cy - d2 * math.sin(phase) - 12, to_name, 0)
  g:colour(255, 255, 255)
  g:disc(cx + d1, cy, 4)
  g:text(cx + d1 + 7, cy - 12, from_name, 0)
end

local burn_dv0 = 0

return {
  name = "Transfer",
  buttons = { "TGT+", "TGT-", "AUTO", "ABORT", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "TRANSFER", 1)
    if not props.get("/target/valid") then
      g:colour(255, 200, 80)
      g:text(200, 200, "No target", 1)
      g:text(200, 222, "Press TGT+ / TGT- to select", 1)
      return
    end
    g:colour(120, 255, 140)
    g:text(200, 36, "Target: " .. props.get("/sim/target"), 1)
    if not props.get("/target/in-plane-data") then
      g:colour(255, 200, 80)
      g:text(200, 200, "Target not in this orbit's frame", 1)
      g:text(200, 224, "To leave for another body, use the", 1)
      g:text(200, 242, "ORBITCOMP Plan page (it plans departures)", 1)
      return
    end
    local mu = soi_gm()
    local r1, r2 = props.get("/target/ship-radius"), props.get("/target/radius")
    local a = (r1 + r2) / 2
    local v1, v2 = math.sqrt(mu / r1), math.sqrt(mu / r2)
    local dv1 = v1 * (math.sqrt(2 * r2 / (r1 + r2)) - 1)
    local dv2 = v2 * (1 - math.sqrt(2 * r1 / (r1 + r2)))
    local tof = math.pi * math.sqrt(a ^ 3 / mu)
    local ns, nt = math.sqrt(mu / r1 ^ 3), math.sqrt(mu / r2 ^ 3)
    local lead = math.pi - nt * tof -- where the target must be, ahead of the ship, at departure
    local phase = math.rad(props.get("/target/phase-angle"))
    local wait = nil
    if math.abs(ns - nt) > 1e-12 then
      wait = ((lead - phase) / (nt - ns)) % (2 * math.pi / math.abs(ns - nt))
    end

    local state = props.get("/autopilot/burn/state")
    local armed = props.get("/autopilot/burn/armed")
    g:text(52, 70, string.format("Burn 1   %+8.1f m/s", dv1), 0)
    g:text(52, 90, string.format("Burn 2   %+8.1f m/s", dv2), 0)
    g:text(52, 110, string.format("Total    %8.1f m/s", math.abs(dv1) + math.abs(dv2)), 0)
    local thrust, mass = ship("max-thrust"), math.max(ship("mass"), 1)
    local accel = thrust > 0 and thrust / mass or 0
    -- Burn 1 time at the present thrust, and the time still to burn: from the autopilot's dV left, or when flying by hand
    -- from the speed the transfer ellipse needs at the present radius against the speed now.
    local burn1 = accel > 0 and math.abs(dv1) / accel or nil
    local left_dv = nil
    if state == "burn" and armed then
      left_dv = props.get("/autopilot/burn/remaining")
    elseif ship("thrust") > 0 and dv1 > 0 then
      local sa = ship("orbit/semi-major-axis")
      local rs = r1
      if sa and rs and rs > 0 and sa > 0 and sa < a then
        left_dv = math.max(0, math.sqrt(mu * (2 / rs - 1 / a)) - math.sqrt(mu * (2 / rs - 1 / sa)))
      end
    end
    if left_dv and accel > 0 then
      g:colour(255, 120, 90)
      g:text(52, 128, string.format("Burn left  %s  (dV %.0f m/s)", minsec(left_dv / accel), left_dv), 0)
      g:colour(120, 255, 140)
    else
      g:text(52, 128, "Burn 1 time  " .. (burn1 and minsec(burn1) or "--"), 0)
    end
    g:text(52, 150, "Flight time  " .. fmt_time(tof), 0)
    g:text(52, 168, string.format("Needed phase  %.1f deg", math.deg(lead)), 0)
    g:text(52, 186, string.format("Current phase %.1f deg", math.deg(phase)), 0)
    g:colour(255, 200, 80)
    if armed and state == "wait" then
      local t = props.get("/autopilot/burn/ignition-in")
      g:text(52, 210, "AUTO: ignition in  " .. fmt_time(t), 0)
      g:text(52, 228, string.format("Burn time ~%s, holding prograde", fmt_time(props.get("/autopilot/burn/duration"))), 0)
      burn_dv0 = props.get("/autopilot/burn/remaining")
    elseif armed and state == "burn" then
      local left = props.get("/autopilot/burn/remaining")
      g:colour(255, 120, 90)
      g:text(52, 210, string.format("BURNING   dV LEFT  %.0f m/s", left), 0)
      if left > burn_dv0 then burn_dv0 = left end
      bar(g, 52, 232, 320, burn_dv0 > 0 and 1 - left / burn_dv0 or 0)
    elseif state == "done" then
      g:colour(120, 255, 140)
      g:text(52, 210, "Burn complete - trim your approach", 0)
    else
      g:text(52, 210, "Next window in  " .. fmt_time(wait), 0)
      g:text(52, 228, "Press AUTO to fly the burn", 0)
    end
    g:colour(120, 255, 140)

    transfer_diagram(g, 200, 318, 56, r1, r2, phase, lead, ship("soi"), "You", props.get("/sim/target"),
      { a = ship("orbit/semi-major-axis"), e = ship("orbit/eccentricity"), nu = ship("orbit/true-anomaly") })
  end,

  button = function(self, i)
    if i == 1 then cycle_target(1) elseif i == 2 then cycle_target(-1)
    elseif i == 3 then arm()
    elseif i == 4 then
      props.set("/autopilot/burn/armed", false)
      props.set("/autopilot/burn/state", "")
      props.set("/controls/throttle", 0.0)
      props.set("/controls/sas", "stability")
    end
  end,
}
