-- Sync Orbit MFD: range, closing speed and phase angle to the target, with the period ratio you need to close the gap.

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

local function fmt_len(m)
  local a = math.abs(m)
  if a >= 1e9 then return string.format("%.2f Gm", m / 1e9) end
  if a >= 1e6 then return string.format("%.1f Mm", m / 1e6) end
  if a >= 1e3 then return string.format("%.1f km", m / 1e3) end
  return string.format("%.0f m", m)
end

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

local function fmt_time(t)
  if t == nil or t < 0 then return "--" end
  return string.format("%d:%02d:%02d", math.floor(t / 3600), math.floor((t % 3600) / 60), math.floor(t % 60))
end

local function soi_body()
  local name = ship("soi")
  for i = 0, props.get("/universe/bodies/count") - 1 do
    if props.get("/universe/bodies/body[" .. i .. "]/name") == name then
      return props.get("/universe/bodies/body[" .. i .. "]/gm")
    end
  end
  return 0
end

local function fmt_dv(dv)
  local a = math.abs(dv)
  return string.format(a < 10 and "%.2f" or "%.1f", a)
end

local function burn_dir(dv)
  return dv >= 0 and "prograde" or "retrograde"
end

-- Returns a list of text lines for the burn plan, assuming near-circular, coplanar orbits.
local function plan(orbits, phase)
  local mu = soi_body()
  local rs, rt = props.get("/target/ship-radius"), props.get("/target/radius")
  local ecc = ship("orbit/eccentricity")
  local Tt = props.get("/target/orbit/period")
  local R = ship("body-radius")
  if mu <= 0 or not Tt or Tt <= 0 then return { "No plan: target has no orbit" } end

  if ecc >= 0.002 then
    -- In an eccentric (phasing) orbit: the matching burn is at the apsis nearest the target's radius.
    local a = ship("orbit/semi-major-axis")
    local ra, rp = ship("orbit/apoapsis") + R, ship("orbit/periapsis") + R
    local at_apo = math.abs(ra - rt) < math.abs(rp - rt)
    local rb = at_apo and ra or rp
    local dv = math.sqrt(mu / rt) - math.sqrt(mu * (2 / rb - 1 / a))
    local tb = at_apo and ship("orbit/time-to-apoapsis") or ship("orbit/time-to-periapsis")
    return {
      string.format("Match orbit at %s", at_apo and "apoapsis" or "periapsis"),
      string.format("Burn in %s", fmt_time(tb)),
      string.format("dV %.1f m/s %s", math.abs(dv), burn_dir(dv)),
    }
  end

  if math.abs(rs - rt) > 0.02 * rt then
    -- Circular orbits at different heights: Hohmann transfer, burn when the phase is right.
    local a = (rs + rt) / 2
    local dv1 = math.sqrt(mu / rs) * (math.sqrt(2 * rt / (rs + rt)) - 1)
    local dv2 = math.sqrt(mu / rt) * (1 - math.sqrt(2 * rs / (rs + rt)))
    local tof = math.pi * math.sqrt(a ^ 3 / mu)
    local ns, nt = math.sqrt(mu / rs ^ 3), math.sqrt(mu / rt ^ 3)
    local lead = math.pi - nt * tof
    local wait = ((lead - math.rad(phase)) / (nt - ns)) % (2 * math.pi / math.abs(ns - nt))
    return {
      "Different orbit: Hohmann transfer",
      string.format("Burn 1 in %s", fmt_time(wait)),
      string.format("dV %.1f m/s %s", math.abs(dv1), burn_dir(dv1)),
      string.format("Burn 2 %.1f m/s %s", math.abs(dv2), burn_dir(dv2)),
      string.format("%s after burn 1", fmt_time(tof)),
    }
  end

  -- Same orbit: burn into a phasing orbit whose period closes the gap after the chosen number of orbits.
  -- More orbits make a gentler phasing orbit; skip any that would dip below the surface.
  local Tp, ap
  for n = orbits, 40 do
    orbits = n
    Tp = Tt * (n - phase / 360) / n
    ap = (mu * (Tp / (2 * math.pi)) ^ 2) ^ (1 / 3)
    if Tp > 0 and 2 * ap - rs > R + 1000 and 2 * ap - rs < 2 * rs then break end
  end
  local v1 = math.sqrt(mu / rs)
  local dv1 = math.sqrt(mu * (2 / rs - 1 / ap)) - v1
  if math.abs(rs * math.rad(phase)) < 30 then return { "Phased with target" } end
  return {
    string.format("Phasing: %d orbit%s", orbits, orbits > 1 and "s" or ""),
    string.format("Burn 1 NOW  %s m/s %s", fmt_dv(dv1), burn_dir(dv1)),
    string.format("Burn 2 %s m/s %s", fmt_dv(dv1), burn_dir(-dv1)),
    string.format("%s after burn 1", fmt_time(Tp * orbits)),
    string.format("Total %s m/s", fmt_dv(2 * dv1)),
  }
end

local orbits = 1
local approach = false

local function linear_approach(rx, ry, rz, vx, vy, vz)
  local range2 = rx * rx + ry * ry + rz * rz
  local speed2 = vx * vx + vy * vy + vz * vz
  local range = math.sqrt(range2)
  local closing = range > 0 and -(rx * vx + ry * vy + rz * vz) / range or 0
  local lateral = math.sqrt(math.max(0, speed2 - closing * closing))
  local time = speed2 > 1e-12 and -(rx * vx + ry * vy + rz * vz) / speed2 or 0
  time = math.max(0, time)
  local x, y, z = rx + vx * time, ry + vy * time, rz + vz * time
  return { range = range, speed = math.sqrt(speed2), closing = closing, lateral = lateral,
           time = time, miss = math.sqrt(x * x + y * y + z * z) }
end

local function draw_approach(g)
  g:colour(255, 240, 140)
  g:text(200, 14, "SYNC APPROACH", 1)
  if not props.get("/target/valid") or props.get("/target/kind") ~= "ship" then
    g:colour(255, 200, 80)
    g:text(200, 200, "Select a target ship", 1)
    return
  end
  local target
  for i = 0, props.get("/ships/count") - 1 do
    local path = "/ships/ship[" .. i .. "]/"
    if i ~= props.get("/sim/active-ship") and props.get(path .. "name") == props.get("/sim/target") then target = path end
  end
  if not target then
    g:colour(255, 200, 80)
    g:text(200, 200, "Target ship unavailable", 1)
    return
  end
  local function delta(field, axis)
    return props.get(target .. field .. "/" .. axis) - ship(field .. "/" .. axis)
  end
  local a = linear_approach(delta("position", "x"), delta("position", "y"), delta("position", "z"),
                            delta("velocity", "x"), delta("velocity", "y"), delta("velocity", "z"))
  g:colour(120, 255, 140)
  g:text(200, 42, "Target: " .. props.get("/sim/target"), 1)
  g:text(40, 78, "Range           " .. fmt_len(a.range), 0)
  g:text(40, 100, string.format("Relative speed  %.2f m/s", a.speed), 0)
  g:text(40, 122, string.format("Closing         %+.2f m/s", a.closing), 0)
  g:text(40, 144, string.format("Sideways speed  %.2f m/s", a.lateral), 0)
  g:colour(255, 200, 80)
  if not props.get("/target/same-soi") or a.range > 100000 or a.time > 300 then
    g:text(200, 204, "Straight-line estimate unavailable", 1)
    g:text(200, 226, "Requires same SOI, <100 km, <5 min", 1)
  elseif a.closing <= 0 or a.speed < 1e-6 then
    g:text(200, 204, "Not approaching", 1)
  else
    g:text(40, 204, "Linear closest  " .. fmt_len(a.miss), 0)
    g:text(40, 226, "In              " .. fmt_time(a.time), 0)
  end
  g:colour(255, 200, 80)
  g:text(200, 280, "Estimate only: gravity and burns change it", 1)
  g:text(200, 302, "Reduce relative speed before docking", 1)
  g:text(200, 324, "Use Docking MFD for port alignment", 1)
end

return {
  name = "Sync Orbit",
  buttons = { "TGT+", "TGT-", "ORB+", "ORB-", "APPR", "", "", "", "", "", "", "" },
  _test = { linear_approach = linear_approach },
  init = function(self) end,

  draw = function(self, g, w, h)
    if approach then draw_approach(g) return end
    g:colour(255, 240, 140)
    g:text(200, 14, "SYNC ORBIT", 1)
    if not props.get("/target/valid") then
      g:colour(255, 200, 80)
      g:text(200, 200, "No target", 1)
      g:text(200, 222, "Press TGT+ / TGT- to select", 1)
      return
    end
    g:colour(120, 255, 140)
    g:text(200, 36, "Target: " .. props.get("/sim/target"), 1)
    g:text(52, 64, "Range      " .. fmt_len(props.get("/target/distance")), 0)
    g:text(52, 82, string.format("Rel. speed  %.1f m/s", props.get("/target/relative-speed")), 0)
    g:text(52, 100, string.format("Closing     %.1f m/s", props.get("/target/closing-speed")), 0)
    if not props.get("/target/in-plane-data") then
      g:colour(255, 200, 80)
      g:text(200, 220, "Target not in this orbit's frame", 1)
      return
    end
    local phase = props.get("/target/phase-angle")
    -- Seen from above your orbital plane: your real orbit (green, an ellipse once you are on a transfer), the target's
    -- orbit (orange) and a dot for each. Radii are compressed when the orbits differ a lot, so it is not to scale.
    local cx, cy, R = 200, 272, 64
    local rs, rt = props.get("/target/ship-radius"), props.get("/target/radius")
    local ecc, sma = ship("orbit/eccentricity"), ship("orbit/semi-major-axis")
    local nu = math.rad(-ship("orbit/true-anomaly"))
    local bound = ship("orbit/bound") and ecc < 0.98 and sma > 0
    local p = bound and sma * (1 - ecc * ecc) or rs
    local rmax, rmin = math.max(rs, rt), math.min(rs, rt)
    if bound then rmax = math.max(rmax, p / (1 - ecc)); rmin = math.min(rmin, p / (1 + ecc)) end
    local k = 1
    if rmin / rmax < 0.32 then k = math.min(1, math.log(0.32) / math.log(rmin / rmax)) end
    local function disp(r) return R * (r / rmax) ^ k end
    local dt, ds = disp(rt), disp(rs)
    local a = math.rad(phase)
    g:colour(90, 140, 255)
    g:disc(cx, cy, 3)
    g:colour(255, 140, 60)
    g:circle(cx, cy, dt)
    g:colour(120, 255, 140)
    if bound and ecc > 0.02 then
      local px, py
      for n = 0, 72 do
        local t = 2 * math.pi * n / 72
        local d = disp(p / (1 + ecc * math.cos(t - nu)))
        local x, y = cx + d * math.cos(t), cy - d * math.sin(t)
        if px then g:line(px, py, x, y) end
        px, py = x, y
      end
    else
      g:circle(cx, cy, ds)
    end
    g:colour(255, 255, 255)
    g:disc(cx + ds, cy, 4)
    g:text(cx + ds + 6, cy - 12, "You", 0)
    g:colour(255, 200, 80)
    g:disc(cx + math.cos(a) * dt, cy - math.sin(a) * dt, 4)
    g:text(cx + math.cos(a) * dt + 6, cy - math.sin(a) * dt - 12, props.get("/sim/target"), 0)
    g:colour(70, 90, 130)
    g:line(cx, cy, cx + ds, cy)
    g:line(cx, cy, cx + math.cos(a) * dt, cy - math.sin(a) * dt)
    local lines = plan(orbits, phase)
    g:colour(255, 200, 80)
    for i, t in ipairs(lines) do g:text(200, 118 + (i - 1) * 17, t, 1) end
    g:colour(120, 255, 140)
    g:text(52, 355, string.format("Phase  %.1f deg", phase), 0)
    local pt = props.get("/target/orbit/period")
    local ps = ship("orbit/period")
    if pt and pt > 0 and ps > 0 then g:text(370, 355, string.format("Tship/Ttgt %.3f", ps / pt), 2) end
    g:text(52, 372, string.format("Rel. inc  %.2f deg", props.get("/target/relative-inclination")), 0)
  end,

  button = function(self, i)
    if i == 1 then cycle_target(1) elseif i == 2 then cycle_target(-1)
    elseif i == 3 then orbits = math.min(orbits + 1, 20)
    elseif i == 4 then orbits = math.max(orbits - 1, 1)
    elseif i == 5 then approach = not approach end
  end,
}
