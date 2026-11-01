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

return {
  name = "Sync Orbit",
  buttons = { "TGT+", "TGT-", "ORB+", "ORB-", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
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
    local cx, cy, r1, r2 = 200, 272, 38, 66
    g:colour(60, 110, 255)
    g:circle(cx, cy, r1)
    g:circle(cx, cy, r2)
    g:colour(255, 255, 255)
    g:disc(cx + r1, cy, 4)
    local a = math.rad(phase)
    g:colour(255, 200, 80)
    g:disc(cx + math.cos(a) * r2, cy - math.sin(a) * r2, 4)
    g:colour(120, 255, 140)
    g:line(cx, cy, cx + r1, cy)
    g:line(cx, cy, cx + math.cos(a) * r2, cy - math.sin(a) * r2)
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
    elseif i == 4 then orbits = math.max(orbits - 1, 1) end
  end,
}
