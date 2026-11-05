-- OrbitComp MFD: an orbital computer in the spirit of Orbiter's TransX.
--   ORB  the ship's orbit and the target's, with closest approach.
--   MAN  a manoeuvre node: set the burn time and prograde / radial / normal delta-v, see the orbit you would get,
--        the burn length, and how close you would pass the target.
--   HEL  the orbit about the star the ship would follow after leaving the sphere of influence it is in (an escape
--        orbit), with the planet's orbit and the target's, and closest approach.
--   PLN  a Hohmann transfer for near-circular, coplanar orbits, an ejection burn for a planet around the same star,
--        or a Lambert intercept for eccentric/inclined same-frame orbits. TO MAN loads the plan into a node.
--   Lambert intercepts use two-body propagation; no arrival capture burn is planned.

local TAU, PI = 2 * math.pi, math.pi
local LX = 40 -- text margin, clear of the side button labels
local atan2 = math.atan2 or function(y, x) return math.atan(y, x) end

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

-- Vectors are {x, y, z} tables in the universe frame.
local function add(a, b) return { a[1] + b[1], a[2] + b[2], a[3] + b[3] } end
local function sub(a, b) return { a[1] - b[1], a[2] - b[2], a[3] - b[3] } end
local function mul(a, k) return { a[1] * k, a[2] * k, a[3] * k } end
local function dot(a, b) return a[1] * b[1] + a[2] * b[2] + a[3] * b[3] end
local function cross(a, b) return { a[2] * b[3] - a[3] * b[2], a[3] * b[1] - a[1] * b[3], a[1] * b[2] - a[2] * b[1] } end
local function len(a) return math.sqrt(dot(a, a)) end
local function unit(a)
  local l = len(a)
  if l < 1e-12 then return { 0, 0, 0 } end
  return mul(a, 1 / l)
end
local function rotate(v, axis, ang)
  local c, s = math.cos(ang), math.sin(ang)
  return add(add(mul(v, c), mul(cross(axis, v), s)), mul(axis, dot(axis, v) * (1 - c)))
end
local function clamp(x, lo, hi) return math.max(lo, math.min(hi, x)) end
local function sinh(x) return (math.exp(x) - math.exp(-x)) / 2 end
local function cosh(x) return (math.exp(x) + math.exp(-x)) / 2 end
local function tanh(x) local a, b = math.exp(x), math.exp(-x); return (a - b) / (a + b) end
local function atanh(x) return 0.5 * math.log((1 + x) / (1 - x)) end
local function asinh(x) return math.log(x + math.sqrt(x * x + 1)) end

-- Bodies.
local function body_by_name(name)
  for i = 0, props.get("/universe/bodies/count") - 1 do
    if props.get("/universe/bodies/body[" .. i .. "]/name") == name then return i end
  end
end

local function body_state(i)
  local b = "/universe/bodies/body[" .. i .. "]/"
  return { props.get(b .. "x"), props.get(b .. "y"), props.get(b .. "z") },
         { props.get(b .. "vx"), props.get(b .. "vy"), props.get(b .. "vz") },
         props.get(b .. "gm"), props.get(b .. "parent")
end

-- Orbit from a state relative to the central body. The orbit keeps its own perifocal axes P (towards periapsis), Q
-- and W (the orbit normal), so none of the maths has to deal with angles that go degenerate for circular orbits.
local function make_orbit(r, v, mu)
  local rm, v2 = len(r), dot(v, v)
  local h = cross(r, v)
  local hm = len(h)
  if hm < 1e-9 or rm < 1 or mu <= 0 then return nil end
  local W = mul(h, 1 / hm)
  local ev = mul(sub(mul(r, v2 - mu / rm), mul(v, dot(r, v))), 1 / mu)
  local e = len(ev)
  local P = e > 1e-7 and mul(ev, 1 / e) or unit(r)
  local Q = cross(W, P)
  local p = hm * hm / mu
  local o = { mu = mu, e = e, p = p, P = P, Q = Q, W = W, bound = e < 1 }
  o.nu = atan2(dot(r, Q), dot(r, P))
  o.rp = p / (1 + e)
  if e < 1 then
    o.a = p / (1 - e * e)
    o.ra = p / (1 - e)
    o.n = math.sqrt(mu / o.a ^ 3)
    o.period = TAU / o.n
  elseif e > 1 then
    o.a = p / (1 - e * e)
    o.n = math.sqrt(mu / (-o.a) ^ 3)
  end
  o.inc = math.deg(math.acos(clamp(W[3], -1, 1)))
  local raan = atan2(W[1], -W[2])
  local nodedir = { math.cos(raan), math.sin(raan), 0 }
  o.raan = math.deg(raan) % 360
  o.argp = math.deg(atan2(dot(P, cross(W, nodedir)), dot(P, nodedir))) % 360
  return o
end

local function mean_from_nu(o, nu)
  if o.e < 1 then
    local ea = 2 * math.atan(math.sqrt((1 - o.e) / (1 + o.e)) * math.tan(nu / 2))
    return ea - o.e * math.sin(ea)
  end
  local f = 2 * atanh(clamp(math.sqrt((o.e - 1) / (o.e + 1)) * math.tan(nu / 2), -0.999999999, 0.999999999))
  return o.e * sinh(f) - f
end

local function nu_from_mean(o, m)
  if o.e < 1 then
    m = (m + PI) % TAU - PI
    local ea = o.e > 0.8 and PI or m + o.e * math.sin(m)
    for _ = 1, 40 do
      local d = (ea - o.e * math.sin(ea) - m) / (1 - o.e * math.cos(ea))
      ea = ea - d
      if math.abs(d) < 1e-12 then break end
    end
    return 2 * math.atan(math.sqrt((1 + o.e) / (1 - o.e)) * math.tan(ea / 2))
  end
  local f = asinh(m / o.e)
  for _ = 1, 60 do
    local d = (o.e * sinh(f) - f - m) / (o.e * cosh(f) - 1)
    f = f - d
    if math.abs(d) < 1e-12 then break end
  end
  return 2 * math.atan(math.sqrt((o.e + 1) / (o.e - 1)) * tanh(f / 2))
end

local function advance(o, nu, dt) return nu_from_mean(o, mean_from_nu(o, nu) + o.n * dt) end

-- Seconds from true anomaly nu_from until the orbit reaches nu_to, or nil if a hyperbolic orbit is already past it.
local function time_to(o, nu_from, nu_to)
  local dm = mean_from_nu(o, nu_to) - mean_from_nu(o, nu_from)
  if o.e < 1 then return (dm % TAU) / o.n end
  if dm < -1e-9 then return nil end
  return math.max(dm, 0) / o.n
end

local function state_at(o, nu)
  local c, s = math.cos(nu), math.sin(nu)
  local r = o.p / (1 + o.e * c)
  local k = math.sqrt(o.mu / o.p)
  return add(mul(o.P, r * c), mul(o.Q, r * s)), add(mul(o.P, -k * s), mul(o.Q, k * (o.e + c)))
end

local function max_nu(o)
  return o.e < 1 and PI or math.acos(-1 / o.e) * 0.999
end

local function body_catalog()
  local bodies, by_name, count = {}, {}, props.get("/universe/bodies/count")
  for i = 0, count - 1 do
    local p, v, mu, parent = body_state(i)
    local name = props.get("/universe/bodies/body[" .. i .. "]/name")
    by_name[name] = i
    bodies[i + 1] = {
      name = name,
      p = p, v = v, mu = mu, soi = props.get("/universe/bodies/body[" .. i .. "]/soi"),
      parent_name = parent
    }
  end
  for i, b in ipairs(bodies) do
    b.parent = b.parent_name ~= "" and by_name[b.parent_name] or nil
    b.parent_name = nil
    if b.parent then
      local parent = bodies[b.parent + 1]
      b.orbit = make_orbit(sub(b.p, parent.p), sub(b.v, parent.v), parent.mu)
    end
  end
  return bodies
end

local function body_at(bodies, i, dt, cache)
  local cached = cache[i + 1]
  if cached then return cached[1], cached[2] end
  local b = bodies[i + 1]
  local p, v
  -- Propagate each body on its current parent-relative osculating conic.
  if b.parent and b.orbit then
    local rp, rv = state_at(b.orbit, advance(b.orbit, b.orbit.nu, dt))
    local pp, pv = body_at(bodies, b.parent, dt, cache)
    p, v = add(pp, rp), add(pv, rv)
  else
    p, v = add(b.p, mul(b.v, dt)), b.v
  end
  cache[i + 1] = { p, v }
  return p, v
end

-- Closest approach of two orbits about the same body, from the given anomalies, within `span` seconds.
-- A coarse search selects the encounter; relative-velocity time corrections then refine it.
local function closest(os, nus, ot, nut, span)
  local function relative(t)
    local a, va = state_at(os, advance(os, nus, t))
    local b, vb = state_at(ot, advance(ot, nut, t))
    return sub(a, b), sub(va, vb)
  end
  local n, best, bt = 240, 1e300, 0
  for i = 0, n do
    local d = len(relative(span * i / n))
    if d < best then best, bt = d, span * i / n end
  end
  local lo, hi = math.max(0, bt - span / n), math.min(span, bt + span / n)
  local gain, last_correction = 1, 0
  for _ = 1, 30 do
    local position, velocity = relative(bt)
    local speed2 = dot(velocity, velocity)
    if speed2 < 1e-18 then break end
    local correction = -dot(position, velocity) / speed2
    if correction * last_correction < 0 and math.abs(correction / span) > 0.0001 then
      gain = gain * 0.5
    else
      gain = math.min(gain * 1.1, 1)
    end
    last_correction = correction
    if correction < 0 then hi = bt else lo = bt end
    local next_t = clamp(bt + correction * gain, lo, hi)
    if math.abs(next_t - bt) < math.max(1e-6, span * 1e-12) then
      bt = next_t
      break
    end
    bt = next_t
  end
  return len(relative(bt)), bt
end

-- How far ahead to look for a closest approach: one orbit, or for an escape orbit until it leaves the target's orbit.
local function look_span(os, nus, ot)
  if os.bound then return os.period end
  local far = 3 * (ot.ra or ot.rp * 2)
  local c = clamp((os.p / far - 1) / os.e, -1, 1)
  local t = time_to(os, nus, math.acos(c))
  return t and math.max(t, 60) or ot.period or 86400
end

-- The ship, its orbit about the body whose sphere of influence it is in, and the target if it shares that body.
local function ship_frame()
  local soi = ship("soi")
  local idx = body_by_name(soi)
  if not idx then return nil end
  local bp, bv, mu, parent = body_state(idx)
  local r = sub({ ship("position/x"), ship("position/y"), ship("position/z") }, bp)
  local v = sub({ ship("velocity/x"), ship("velocity/y"), ship("velocity/z") }, bv)
  local o = make_orbit(r, v, mu)
  if not o then return nil end
  return { soi = soi, idx = idx, mu = mu, parent = parent, bp = bp, bv = bv, r = r, v = v, o = o }
end

local function target_name() return props.get("/sim/target") end

-- Patch an escaping post-burn conic onto its parent's orbit, then refine the encounter against a sibling body.
local function patched_encounter(f, nd)
  if not nd or not nd.after or nd.after.e <= 1 or not f.parent or f.parent == "" or
     props.get("/target/kind") ~= "body" then return nil end
  local catalog = body_catalog()
  local parent_idx = body_by_name(f.parent)
  local target_idx = body_by_name(target_name())
  if not parent_idx or not target_idx or target_idx == f.idx then return nil end
  local soi = catalog[f.idx + 1].soi
  if not soi or soi <= len(nd.r) then return nil end
  if f.o.e > 1e-7 and f.o.p / (1 + f.o.e * math.cos(f.o.nu)) < soi then
    local nu_exit = math.acos(clamp((f.o.p / soi - 1) / f.o.e, -1, 1))
    local current_exit = time_to(f.o, f.o.nu, nu_exit)
    if current_exit and current_exit < nd.t then return nil end
  end

  local after, nu0 = nd.after, nd.after.nu
  local cos_exit = clamp((after.p / soi - 1) / after.e, -1, 1)
  local nu_exit = math.acos(cos_exit)
  local exit_in = time_to(after, nu0, nu_exit)
  if not exit_in then return nil end
  local exit_nu = advance(after, nu0, exit_in)
  local exit_r, exit_v = state_at(after, exit_nu)
  local exit_time = nd.t + exit_in
  local cache = {}
  local body_p, body_v = body_at(catalog, f.idx, exit_time, cache)
  local parent_p, parent_v = body_at(catalog, parent_idx, exit_time, cache)
  local ship_parent = make_orbit(sub(add(body_p, exit_r), parent_p), sub(add(body_v, exit_v), parent_v),
                                 catalog[parent_idx + 1].mu)
  if target_idx == parent_idx then
    if not ship_parent or not ship_parent.n then return nil end
    local closest_in = time_to(ship_parent, ship_parent.nu, 0)
    if not closest_in then return nil end
    return { distance = ship_parent.rp, time = exit_time + closest_in }
  end
  if catalog[target_idx + 1].parent ~= parent_idx then return nil end
  local target = catalog[target_idx + 1]
  if not ship_parent or not ship_parent.n or not target.orbit or not target.orbit.n then return nil end

  local target_nu = advance(target.orbit, target.orbit.nu, exit_time)
  local span = look_span(ship_parent, ship_parent.nu, target.orbit)
  if ship_parent.bound and target.orbit.bound then
    local beat = math.abs(ship_parent.n - target.orbit.n)
    if beat > 1e-12 then span = math.min(TAU / beat, 10 * 365.25 * 86400) end
  end
  local distance, closest_in = closest(ship_parent, ship_parent.nu, target.orbit, target_nu, span)
  local sp, tp = state_at(ship_parent, advance(ship_parent, ship_parent.nu, closest_in)), state_at(target.orbit, advance(target.orbit, target_nu, closest_in))
  return { distance = distance, time = exit_time + closest_in,
           geo = { path = ship_parent, target_orbit = target.orbit, planet_orbit = catalog[f.idx + 1].orbit,
                   ship_at = sp, target_at = tp, exit_at = (state_at(ship_parent, ship_parent.nu)) } }
end

local function target_state(f)
  if not props.get("/target/valid") then return nil end
  local name = target_name()
  if props.get("/target/kind") == "ship" then
    for j = 0, props.get("/ships/count") - 1 do
      local b = "/ships/ship[" .. j .. "]/"
      if props.get(b .. "name") == name then
        if props.get(b .. "soi") ~= f.soi then return nil end
        local r = sub({ props.get(b .. "position/x"), props.get(b .. "position/y"), props.get(b .. "position/z") }, f.bp)
        local v = sub({ props.get(b .. "velocity/x"), props.get(b .. "velocity/y"), props.get(b .. "velocity/z") }, f.bv)
        return make_orbit(r, v, f.mu), r, v
      end
    end
    return nil
  end
  local i = body_by_name(name)
  if not i or i == f.idx then return nil end
  local tp, tv, _, parent = body_state(i)
  if parent ~= f.soi then return nil end
  local r, v = sub(tp, f.bp), sub(tv, f.bv)
  return make_orbit(r, v, f.mu), r, v
end

-- Ejection: the target is a planet about the star the ship's body also orbits.
local function sibling_state(f)
  if not props.get("/target/valid") or props.get("/target/kind") ~= "body" or not f.parent or f.parent == "" then return nil end
  local i = body_by_name(target_name())
  if not i or i == f.idx then return nil end
  local tp, tv, _, parent = body_state(i)
  if parent ~= f.parent then return nil end
  local sp, sv, smu = body_state(body_by_name(f.parent))
  return { rt = sub(tp, sp), vt = sub(tv, sv), rp = sub(f.bp, sp), vp = sub(f.bv, sv), mu = smu }
end

-- Return: the target is the body the ship's body orbits (Moon to Earth).
local function parent_state(f)
  if not props.get("/target/valid") or props.get("/target/kind") ~= "body" or not f.parent or f.parent == "" then return nil end
  local i = body_by_name(target_name())
  if not i or i ~= body_by_name(f.parent) then return nil end
  local pp, pv, pmu = body_state(i)
  return { rm = sub(f.bp, pp), vm = sub(f.bv, pv), mu = pmu, radius = props.get("/universe/bodies/body[" .. i .. "]/radius") }
end

-- Formatting.
local function fmt_len(m)
  local a = math.abs(m)
  if a >= 1e12 then return string.format("%.2f Tm", m / 1e12) end
  if a >= 1e9 then return string.format("%.2f Gm", m / 1e9) end
  if a >= 1e6 then return string.format("%.1f Mm", m / 1e6) end
  if a >= 1e3 then return string.format("%.1f km", m / 1e3) end
  return string.format("%.0f m", m)
end

local function fmt_time(s)
  if s == nil or s ~= s then return "--" end
  local sign = s < 0 and "-" or ""
  s = math.abs(s)
  local d = math.floor(s / 86400)
  local h, m = math.floor((s % 86400) / 3600), math.floor((s % 3600) / 60)
  if d > 0 then return string.format("%s%dd %02d:%02d:%02d", sign, d, h, m, math.floor(s % 60)) end
  return string.format("%s%d:%02d:%02d", sign, h, m, math.floor(s % 60))
end

-- dV delivered by the engines since the current burn began (the burn ends after 10 s without thrust); the burn-left lines use it.
local burn = { done = 0, last = nil, idle = 0 }
local function track_burn()
  local now = props.get("/sim/time")
  local dt = burn.last and math.max(0, math.min(now - burn.last, 1)) or 0
  burn.last = now
  local thrust, mass = ship("thrust"), math.max(ship("mass"), 1)
  if thrust > 0 then
    burn.done = burn.done + thrust / mass * dt
    burn.idle = 0
  else
    burn.idle = burn.idle + dt
    if burn.idle > 10 then burn.done = 0 end
  end
end

local function burn_time(dv)
  local thrust, mass = ship("max-thrust"), math.max(ship("mass"), 1)
  if thrust <= 0 then return nil end
  return dv * mass / thrust
end

-- Burn left for a planned dV, shown once the engines have fired.
local function burn_left_line(g, x, y, dv)
  if burn.done <= 0.5 then return false end
  local left = math.max(0, dv - burn.done)
  local t = burn_time(left)
  g:colour(255, 120, 90)
  g:text(x, y, string.format("Burn left %s  dV %.0f", t and fmt_time(t) or "--", left), 0)
  return true
end

-- Warns when the active stage cannot deliver the planned dV; the burn would stop short and the transfer fall short of the target.
local function dv_short(dv)
  local have = ship("stage-delta-v")
  return have > 0 and dv > have * 1.02, have
end

local function dv_warning(g, dv)
  local short, have = dv_short(dv)
  if not short then return end
  g:colour(255, 70, 70)
  g:text(LX, 46, string.format("NOT ENOUGH dV: need %.0f, stage has %.0f", dv, have), 0)
end

local function fmt_dv(x) return string.format("%+.1f m/s", x) end

-- Target selection: the central star, the planets and the quantum portal are always offered. Moons, stations and other
-- ships appear only when we orbit the body they orbit (or we orbit the moon itself).
local function target_list()
  local list = {}
  local active = props.get("/sim/active-ship")
  local here = props.get("/ships/ship[" .. active .. "]/soi") or ""
  for i = 0, props.get("/ships/count") - 1 do
    if i ~= active then
      local base = "/ships/ship[" .. i .. "]/"
      if props.get(base .. "is-portal") or props.get(base .. "soi") == here then
        list[#list + 1] = props.get(base .. "name")
      end
    end
  end
  local parents = {}
  local n = props.get("/universe/bodies/count")
  for i = 0, n - 1 do
    local b = "/universe/bodies/body[" .. i .. "]/"
    parents[props.get(b .. "name")] = props.get(b .. "parent") or ""
  end
  for i = 0, n - 1 do
    local name = props.get("/universe/bodies/body[" .. i .. "]/name")
    local parent = parents[name]
    local is_moon = parent ~= "" and (parents[parent] or "") ~= ""
    if not is_moon or parent == here or name == here then list[#list + 1] = name end
  end
  return list
end

local function cycle_target(dir)
  local list = target_list()
  local cur, idx = target_name(), 0
  for i, n in ipairs(list) do if n == cur then idx = i end end
  idx = (idx - 1 + dir) % #list + 1
  props.set("/sim/target", list[idx])
end

-- Manoeuvre state; shared by both MFD units so the plan survives switching pages.
local PAGES = { "ORB", "MAN", "PLN", "HEL" }
local NODE_MODES = { "T+", "Pe", "Ap", "AN", "DN" }
local FIELDS = { "Pro", "Rad", "Nrm", "Time" }
local DV_STEPS = { 0.1, 1, 10, 100, 1000 }
local TIME_STEPS = { 1, 10, 60, 600, 3600 }
local page, zoom, step = 1, 1.0, 3
local node = { mode = 1, time = 600, dv = { 0, 0, 0 }, field = 1 }
local frame_count, scan_cache = 0, nil

-- Where the node is in the ship's orbit and the state just after the burn.
local function compute_node(f, ot)
  local o = f.o
  local nu_n, t_n
  local mode = NODE_MODES[node.mode]
  if mode == "T+" then
    t_n = math.max(node.time, 0)
    nu_n = advance(o, o.nu, t_n)
    if not o.bound and nu_n > max_nu(o) then return nil end
  elseif mode == "Pe" then
    nu_n = 0
  elseif mode == "Ap" and o.bound then
    nu_n = PI
  elseif mode == "AN" or mode == "DN" then
    local wt = ot and ot.W or { 0, 0, 1 }
    local line = cross(wt, o.W)
    if len(line) < 1e-9 then return nil end
    local a = atan2(dot(line, o.Q), dot(line, o.P))
    local _, va = state_at(o, a)
    local ascending = dot(va, wt) > 0
    nu_n = ((mode == "AN") == ascending) and a or a + PI
  else
    nu_n = 0
  end
  if not t_n then
    t_n = time_to(o, o.nu, nu_n)
    if not t_n then return nil end
  end
  local rn, vn = state_at(o, nu_n)
  local pro = unit(vn)
  local nrm = unit(cross(rn, vn))
  local rad = cross(pro, nrm)
  local dvv = add(add(mul(pro, node.dv[1]), mul(rad, node.dv[2])), mul(nrm, node.dv[3]))
  local after = make_orbit(rn, add(vn, dvv), f.mu)
  local dv = len(dvv)
  local mass, thrust = ship("mass"), ship("max-thrust")
  return { t = t_n, nu = nu_n, r = rn, v = vn, dvv = dvv, dv = dv, after = after,
           burn = (thrust and thrust > 0 and mass) and dv * mass / thrust or nil }
end

-- Closest approach of the ship, on its current or its post-burn orbit, to the target, refreshed a few times a second.
local function scan(f, ot, nd)
  if scan_cache and frame_count - scan_cache.at < 12 then return scan_cache end
  local res = { at = frame_count }
  if ot then
    local span = look_span(f.o, f.o.nu, ot)
    res.now_d, res.now_t = closest(f.o, f.o.nu, ot, ot.nu, span)
    if nd and nd.after and nd.dv > 0 then
      local nut = advance(ot, ot.nu, nd.t)
      local nus = nd.after.nu
      local sp = look_span(nd.after, nus, ot)
      local d, t = closest(nd.after, nus, ot, nut, sp)
      res.man_d, res.man_t = d, nd.t + t
    end
  elseif nd then
    local patched = patched_encounter(f, nd)
    if patched then
      res.man_d, res.man_t, res.patched, res.geo = patched.distance, patched.time, true, patched.geo
    end
  end
  scan_cache = res
  return res
end

-- Draw an orbit as a polyline around (cx, cy); `axes` give the plane to project onto.
local function draw_orbit(g, o, P, Q, cx, cy, scale, limit)
  local lo, hi = 0, TAU
  if not o.bound then hi = max_nu(o); lo = -hi end
  local px, py
  for i = 0, 96 do
    local t = lo + (hi - lo) * i / 96
    local r = o.p / (1 + o.e * math.cos(t))
    if r > 0 and r < limit then
      local pos = add(mul(o.P, r * math.cos(t)), mul(o.Q, r * math.sin(t)))
      local x, y = cx + dot(pos, P) * scale, cy - dot(pos, Q) * scale
      if px then g:line(px, py, x, y) end
      px, py = x, y
    else
      px = nil
    end
  end
end

-- A point beyond the plot area is pinned to its edge along the same bearing, so a marker never leaves the display.
local PLOT_EDGE = 135
local function pin(v, P, Q, scale, edge)
  edge = edge or PLOT_EDGE
  local dx, dy = dot(v, P) * scale, -dot(v, Q) * scale
  local d = math.sqrt(dx * dx + dy * dy)
  if d > edge then dx, dy = dx * edge / d, dy * edge / d end
  return dx, dy
end

local function plot_point(g, v, P, Q, cx, cy, scale, rad, edge)
  local dx, dy = pin(v, P, Q, scale, edge)
  g:disc(cx + dx, cy + dy, rad)
end

local M = { name = "OrbitComp" }
M.buttons = { "ORB", "MAN", "PLN", "TGT+", "TGT-", "HEL", "", "", "", "", "", "" }

local auto_on = false -- the burn autopilot was armed from this page (see auto_arm)
local DAY = 86400
local FAST_LABELS = { "FAST", "FLIGHT", "DEPART", "BEST" }
-- Fast ejection: mode 0 is the Hohmann plan, 1 adjusts the flight time, 2 the departure date and 3 finds the cheapest
-- departure for a flight no longer than the one set.
local fast = { mode = 0, target = nil }
local fast_ok = false -- the PLN page shows an ejection that can be made fast

local function set_buttons()
  local right = { "", "", "", "", "", "" }
  if page == 1 or page == 4 then right = { "ZOOM+", "ZOOM-", "FIT", "", "", "" }
  elseif page == 2 then right = { "FIELD", "+", "-", "STEP", "NODE", "ZERO" }
  else right = { "TO MAN", M.plan and M.plan.kind == "lambert" and "" or (auto_on and "AUTO*" or "AUTO"),
                 "ABORT", fast_ok and FAST_LABELS[fast.mode + 1] or "", fast_ok and fast.mode > 0 and (fast.mode == 3 and "FIND" or "+") or "",
                 fast_ok and fast.mode > 0 and fast.mode < 3 and "-" or "" } end
  for i = 1, 6 do M.buttons[6 + i] = right[i] end
end

local function title(g, f)
  g:colour(255, 240, 140)
  g:text(200, 14, "ORBITCOMP  " .. PAGES[page] .. "  " .. (f and f.soi or ""), 1)
end

local function need_target(g, y)
  g:colour(255, 200, 80)
  g:text(200, y, "No target - TGT+ / TGT-", 1)
end

local AU = 1.495978707e11

local function draw_orb(g, f, ot, tr)
  title(g, f)
  local o = f.o
  local cx, cy = 200, 215
  local body_r = f.body_r or ship("body-radius")
  local extent = o.bound and o.ra or o.rp * 4
  -- A far larger target orbit (the Moon from low orbit) would squash the ship's own to a dot; ZOOM- brings it in.
  if ot and (ot.ra or ot.rp * 2) < 6 * extent then extent = math.max(extent, ot.ra or ot.rp * 2) end
  -- The ship itself always stays on the plot, whatever the orbit shape (an escape, or a long way out on a hyperbola).
  extent = math.max(extent, len(f.r) * 0.95)
  local scale = 125 / math.max(extent, body_r * 1.05) * zoom
  local limit = 125 / scale * 1.5
  local P, Q = o.P, o.Q
  g:colour(60, 110, 255)
  g:circle(cx, cy, math.max(body_r * scale, 3))
  g:colour(120, 255, 140)
  draw_orbit(g, o, P, Q, cx, cy, scale, limit)
  g:colour(255, 200, 80)
  g:circle(cx + o.rp * scale, cy, 4)
  g:text(cx + o.rp * scale + 8, cy - 8, "Pe", 0)
  if o.bound then
    g:circle(cx - o.ra * scale, cy, 4)
    g:text(cx - o.ra * scale - 8, cy - 8, "Ap", 2)
  end
  if f.home then
    g:colour(80, 160, 255)
    draw_orbit(g, f.home, P, Q, cx, cy, scale, limit)
    plot_point(g, f.home_r, P, Q, cx, cy, scale, 3)
  end
  g:colour(255, 255, 255)
  plot_point(g, f.r, P, Q, cx, cy, scale, 4)
  local scanned = scan(f, ot, nil)
  if ot then
    g:colour(255, 140, 60)
    draw_orbit(g, ot, P, Q, cx, cy, scale, limit)
    plot_point(g, tr, P, Q, cx, cy, scale, 4)
    if scanned and scanned.now_d then
      g:colour(255, 90, 90)
      local ps = state_at(f.o, advance(f.o, f.o.nu, scanned.now_t))
      plot_point(g, ps, P, Q, cx, cy, scale, 3)
    end
  end
  g:colour(120, 255, 140)
  g:text(LX, 36, "T " .. (o.bound and fmt_time(o.period) or "--"), 0)
  if f.home then
    g:text(LX, 52, "Tap " .. fmt_time(time_to(o, o.nu, PI)), 0)
    g:text(LX, 68, string.format("Vel %.2f km/s", len(f.v) / 1000), 0)
    g:text(LX, 84, string.format("V inf %.2f km/s", f.v_inf / 1000), 0)
  else
    g:text(LX, 52, "Tpe " .. fmt_time(ship("orbit/time-to-periapsis")), 0)
    if o.bound then g:text(LX, 68, "Tap " .. fmt_time(ship("orbit/time-to-apoapsis")), 0) end
    g:text(LX, 84, string.format("Vel %.0f m/s", len(f.v)), 0)
    g:text(LX, 100, "Alt " .. fmt_len(ship("altitude")), 0)
  end
  if ot then
    g:colour(255, 140, 60)
    g:text(LX, 124, "Tgt " .. target_name(), 0)
    if scanned and scanned.now_d then
      g:colour(255, 90, 90)
      g:text(LX, 140, "Cls " .. fmt_len(scanned.now_d), 0)
      g:text(LX, 156, "in " .. fmt_time(scanned.now_t), 0)
    end
    g:colour(120, 255, 140)
    g:text(LX, 172, string.format("Rel inc %.2f", math.deg(math.acos(clamp(dot(o.W, ot.W), -1, 1)))), 0)
  elseif props.get("/target/valid") then
    g:colour(255, 200, 80)
    g:text(LX, 124, "Tgt " .. target_name(), 0)
    g:text(LX, 140, "other frame", 0)
  end
  g:colour(120, 255, 140)
  if f.home then
    g:text(LX, 352, string.format("Pe %.3f AU", o.rp / AU), 0)
    g:text(230, 352, "Ap " .. (o.bound and string.format("%.3f AU", o.ra / AU) or "--"), 0)
  else
    g:text(LX, 352, "PeA " .. fmt_len(o.rp - body_r), 0)
    g:text(230, 352, "ApA " .. (o.bound and fmt_len(o.ra - body_r) or "--"), 0)
  end
  g:text(LX, 368, string.format("Ecc %.4f   Inc %.2f", o.e, o.inc), 0)
  g:text(LX, 384, string.format("LAN %.1f   ArgPe %.1f", o.raan, o.argp), 0)
end

-- The orbit about the star the ship would follow once it leaves the sphere of influence it is in: the planet's state plus the
-- escape orbit's excess velocity along its outgoing asymptote. A ship already about the star just shows its own orbit.
local function helio_frame(f)
  if not f.parent or f.parent == "" then return f end
  local o = f.o
  if o.bound then return nil end
  local pidx = body_by_name(f.parent)
  if not pidx then return nil end
  local sp, sv, smu = body_state(pidx)
  local v_inf = math.sqrt(-o.mu / o.a)
  local u = add(mul(o.P, -1 / o.e), mul(o.Q, math.sqrt(1 - 1 / (o.e * o.e))))
  local rp, vp = sub(f.bp, sp), sub(f.bv, sv)
  local r, v = add(rp, f.r), add(vp, mul(u, v_inf))
  local ho = make_orbit(r, v, smu)
  local home = make_orbit(rp, vp, smu)
  if not ho or not home then return nil end
  local _, _, _, grand = body_state(pidx)
  return { soi = f.parent, idx = pidx, mu = smu, parent = grand, bp = sp, bv = sv, r = r, v = v, o = ho,
           body_r = 6.96e8, home = home, home_r = rp, v_inf = v_inf }
end

local function draw_hel(g, f)
  local h = helio_frame(f)
  if not h then
    title(g, f)
    g:colour(255, 200, 80)
    g:text(200, 200, "Bound to " .. f.soi .. " - no escape orbit", 1)
    return
  end
  local ot, tr = target_state(h)
  draw_orb(g, h, ot, tr)
end

-- Tells the sim when the burn this page shows begins, so File > Set time can skip to just before it.
local function publish_burn(centre_in, burn, label)
  props.set("/plan/orbitcomp/at", props.get("/sim/time") + centre_in - (burn or 0) / 2)
  props.set("/plan/orbitcomp/label", label)
  props.set("/plan/orbitcomp/stamp", props.get("/sim/frame"))
end

-- The heliocentric view of a patched encounter: the planet's and the target's orbits, the path the burn gives, and where the
-- ship and the target are when they pass closest. Changing the node moves the path, so the miss distance can be walked to zero.
local function draw_arrival(g, geo, cx, cy)
  local pl = geo.planet_orbit
  local P, Q = pl.P, pl.Q
  local ta, pa = geo.target_orbit, geo.path
  local extent = math.max(pl.ra or pl.rp * 2, ta.ra or ta.rp * 2)
  local scale = 78 / extent
  local limit = 78 / scale * 1.5
  g:colour(255, 220, 90)
  g:disc(cx, cy, 3)
  g:colour(60, 110, 255)
  draw_orbit(g, pl, P, Q, cx, cy, scale, limit)
  g:colour(255, 140, 60)
  draw_orbit(g, ta, P, Q, cx, cy, scale, limit)
  g:colour(255, 255, 255)
  draw_orbit(g, pa, P, Q, cx, cy, scale, limit)
  plot_point(g, geo.exit_at, P, Q, cx, cy, scale, 3, 82)
  g:colour(255, 255, 255)
  plot_point(g, geo.ship_at, P, Q, cx, cy, scale, 4, 82)
  g:colour(255, 140, 60)
  plot_point(g, geo.target_at, P, Q, cx, cy, scale, 4, 82)
  local sx, sy = pin(geo.ship_at, P, Q, scale, 82)
  local tx, ty = pin(geo.target_at, P, Q, scale, 82)
  g:colour(255, 90, 90)
  g:line(cx + sx, cy + sy, cx + tx, cy + ty)
  g:colour(255, 255, 255)
  g:text(cx + sx + 6, cy + sy - 12, "You", 0)
end

local function draw_man(g, f, ot, tr)
  title(g, f)
  local o = f.o
  local nd = compute_node(f, ot)
  local mode = NODE_MODES[node.mode]
  g:colour(120, 255, 140)
  g:text(LX, 34, "Node " .. mode .. (nd and ("   in " .. fmt_time(nd.t)) or "   no such point"), 0)
  local rows = { "Pro", "Rad", "Nrm", "Time" }
  for i = 1, 4 do
    local text
    if i < 4 then text = string.format("%s %+9.1f m/s", rows[i], node.dv[i])
    elseif mode == "T+" then text = string.format("Time %+7.0f s", node.time)
    else text = "Time (at " .. mode .. ")" end
    if node.field == i then g:colour(255, 240, 140) else g:colour(120, 255, 140) end
    g:text(LX, 34 + i * 17, (node.field == i and ">" or " ") .. text, 0)
  end
  g:colour(120, 255, 140)
  local step_text = node.field == 4 and (TIME_STEPS[step] .. " s") or (DV_STEPS[step] .. " m/s")
  g:text(240, 51, "Step " .. step_text, 0)
  if not nd then return end
  g:text(240, 68, string.format("dV %.1f m/s", nd.dv), 0)
  g:text(240, 85, nd.burn and ("Burn " .. fmt_time(nd.burn)) or "Burn --", 0)
  if nd.burn then
    g:colour(255, 200, 80)
    g:text(240, 102, "Ignite T-" .. fmt_time(nd.t - nd.burn / 2), 0)
    if nd.dv > 0 then publish_burn(nd.t, nd.burn, "manoeuvre node") end
    burn_left_line(g, 240, 119, nd.dv)
  end
  local a = nd.after
  g:colour(120, 255, 140)
  local body_r = ship("body-radius")
  if a then
    g:text(LX, 140, "After burn", 0)
    g:text(LX, 157, "PeA " .. fmt_len(a.rp - body_r) .. "   ApA " .. (a.bound and fmt_len(a.ra - body_r) or "escape"), 0)
    g:text(LX, 174, string.format("Ecc %.4f   Inc %.2f", a.e, a.inc), 0)
    if not a.bound then
      g:text(240, 140, string.format("v inf %.0f m/s", math.sqrt(-a.mu / a.a)), 0)
    end
  end
  local scanned = scan(f, ot, nd)
  if scanned and scanned.man_d then
    g:colour(255, 140, 60)
    local line = "Close " .. fmt_len(scanned.man_d) .. " in " .. fmt_time(scanned.man_t)
    if scanned.patched then line = line .. " (SOI patched)" end
    g:text(LX, 191, line, 0)
  elseif ot and scanned then
    g:colour(255, 140, 60)
    local line = scanned.now_d and ("Close " .. fmt_len(scanned.now_d) .. " (no burn)") or "No close approach"
    g:text(LX, 191, line, 0)
  elseif props.get("/target/valid") then
    g:colour(255, 200, 80)
    g:text(LX, 191, "Target is not in " .. f.soi .. "'s orbit frame; PLN plans the ejection", 0)
  end

  if scanned and scanned.geo then
    draw_arrival(g, scanned.geo, 200, 305)
    return
  end
  -- Current orbit dim, post-burn orbit bright, and the node.
  local cx, cy = 200, 305
  local extent = o.bound and o.ra or o.rp * 4
  if a then extent = math.max(extent, a.bound and a.ra or a.rp * 4) end
  extent = math.max(extent, len(f.r) * 0.95, nd.r and len(nd.r) * 0.95 or 0)
  local scale = 80 / math.max(extent, body_r * 1.05) * zoom
  local limit = 80 / scale * 1.5
  g:colour(60, 110, 255)
  g:circle(cx, cy, math.max(body_r * scale, 3))
  g:colour(70, 150, 90)
  draw_orbit(g, o, o.P, o.Q, cx, cy, scale, limit)
  if a and nd.dv > 0 then
    g:colour(255, 255, 255)
    draw_orbit(g, a, o.P, o.Q, cx, cy, scale, limit)
  end
  if ot then
    g:colour(255, 140, 60)
    draw_orbit(g, ot, o.P, o.Q, cx, cy, scale, limit)
  end
  g:colour(255, 255, 255)
  plot_point(g, f.r, o.P, o.Q, cx, cy, scale, 3, 82)
  g:colour(255, 200, 80)
  plot_point(g, nd.r, o.P, o.Q, cx, cy, scale, 4, 82)
  if not props.get("/target/valid") then
    g:colour(120, 160, 120)
    g:text(LX, 385, "TGT+ sets a target for AN/DN and close approach", 0)
  end
end

-- A Hohmann transfer to a target around the same body.
local function plan_hohmann(f, ot, tr, tv)
  local o, mu = f.o, f.mu
  local r1, r2 = len(f.r), len(tr)
  local a = (r1 + r2) / 2
  local v1, v2 = math.sqrt(mu / r1), math.sqrt(mu / r2)
  local dv1 = v1 * (math.sqrt(2 * r2 / (r1 + r2)) - 1)
  local dv2 = v2 * (1 - math.sqrt(2 * r1 / (r1 + r2)))
  local tof = PI * math.sqrt(a ^ 3 / mu)
  local ns = math.sqrt(mu / r1 ^ 3)
  local nt = math.sqrt(mu / r2 ^ 3) * (dot(ot.W, o.W) >= 0 and 1 or -1)
  local lead = (PI - nt * tof) % TAU
  local trp = sub(tr, mul(o.W, dot(tr, o.W)))
  local phase = atan2(dot(cross(f.r, trp), o.W), dot(f.r, trp)) % TAU
  local wait
  if math.abs(ns - nt) > 1e-12 then wait = ((lead - phase) / (nt - ns)) % (TAU / math.abs(ns - nt)) end
  local synodic = math.abs(ns - nt) > 1e-12 and TAU / math.abs(ns - nt) or nil
  return { kind = "hohmann", dv1 = dv1, dv2 = dv2, tof = tof, lead = lead, phase = phase, wait = wait, r2 = r2, again = synodic,
           plane = math.deg(math.acos(clamp(dot(ot.W, o.W), -1, 1))) }
end

-- Single-revolution Lambert solution using universal variables.
-- With `normal` (the direction the body orbits about), a transfer that has to sweep more than half a turn goes the long way.
local function lambert_velocity(r1, r2, tof, mu, normal)
  local r1m, r2m = len(r1), len(r2)
  local cosine = clamp(dot(r1, r2) / (r1m * r2m), -1, 1)
  local angle = math.acos(cosine)
  if normal and dot(cross(r1, r2), normal) < 0 then angle = TAU - angle end
  local sine = math.sin(angle)
  if math.abs(sine) < 1e-8 then return nil end
  local A = sine * math.sqrt(r1m * r2m / (1 - cosine))
  if math.abs(A) < 1e-8 then return nil end

  local function stumpff(z)
    if math.abs(z) < 1e-8 then return 0.5, 1 / 6 end
    if z > 0 then
      local q = math.sqrt(z)
      return (1 - math.cos(q)) / z, (q - math.sin(q)) / (q * q * q)
    end
    local q = math.sqrt(-z)
    return (cosh(q) - 1) / -z, (sinh(q) - q) / (q * q * q)
  end

  local function flight_time(z)
    local C, S = stumpff(z)
    if C <= 0 or C ~= C then return math.huge, 0 end
    local y = r1m + r2m + A * (z * S - 1) / math.sqrt(C)
    if y < 0 or y ~= y then return -1, y end
    return ((y / C) ^ 1.5 * S + A * math.sqrt(y)) / math.sqrt(mu), y
  end

  local lo, hi = -4 * PI * PI + 1e-6, 4 * PI * PI - 1e-6
  if flight_time(hi) < tof then return nil end
  for _ = 1, 80 do
    local mid = (lo + hi) / 2
    if flight_time(mid) < tof then lo = mid else hi = mid end
  end
  local _, y = flight_time((lo + hi) / 2)
  if y <= 0 or y ~= y then return nil end
  local g = A * math.sqrt(y / mu)
  if g == 0 or g ~= g then return nil end
  local velocity = mul(sub(r2, mul(r1, 1 - y / r1m)), 1 / g)
  if velocity[1] ~= velocity[1] or velocity[2] ~= velocity[2] or velocity[3] ~= velocity[3]
    or math.abs(velocity[1]) == math.huge or math.abs(velocity[2]) == math.huge or math.abs(velocity[3]) == math.huge then
    return nil
  end
  return velocity
end

-- Solve a heliocentric (or other shared-frame) intercept using the live osculating states, not circular-orbit radii.
local function plan_lambert(f, ot)
  if not f.o.bound or not ot.bound then return nil end
  local scale = (f.o.a + ot.a) / 2
  if scale <= 0 then return nil end
  local nominal = PI * math.sqrt(scale ^ 3 / f.mu)
  local best
  for i = 0, 20 do
    local tof = nominal * (0.5 + i * 0.05)
    local target_r = state_at(ot, advance(ot, ot.nu, tof))
    local departure_v = lambert_velocity(f.r, target_r, tof, f.mu)
    if departure_v then
      local delta_v = sub(departure_v, f.v)
      local magnitude = len(delta_v)
      if not best or magnitude < best.dv then
        local pro = unit(f.v)
        local normal = unit(cross(f.r, f.v))
        local radial = cross(pro, normal)
        best = { kind = "lambert", dv = magnitude, components = {
          dot(delta_v, pro), dot(delta_v, radial), dot(delta_v, normal)
        }, tof = tof, wait = 0, target_r = target_r }
      end
    end
  end
  return best
end

-- Ejection from the ship's orbit around a planet onto a transfer to a sibling planet. The star's gravity is taken
-- as acting on circular, coplanar planet orbits; the burn is at the hyperbola's periapsis at the ship's present radius.
local function ejection_vinf(s)
  local r1, r2 = len(s.rp), len(s.rt)
  return math.abs(math.sqrt(s.mu * (2 / r1 - 2 / (r1 + r2))) - len(s.vp))
end

local function plan_ejection(f, s)
  local o = f.o
  local r1, r2 = len(s.rp), len(s.rt)
  local a = (r1 + r2) / 2
  local vp = len(s.vp)
  local dirp = unit(s.vp)
  local out = r2 > r1 and 1 or -1
  local vinf = ejection_vinf(s)
  local tof = PI * math.sqrt(a ^ 3 / s.mu)
  local wp = unit(cross(s.rp, s.vp))
  local np = vp / r1
  local nt = len(s.vt) / r2 * (dot(unit(cross(s.rt, s.vt)), wp) >= 0 and 1 or -1)
  local lead = (PI - nt * tof) % TAU
  local rtp = sub(s.rt, mul(wp, dot(s.rt, wp)))
  local phase = atan2(dot(cross(s.rp, rtp), wp), dot(s.rp, rtp)) % TAU
  local rb = len(f.r)
  -- The burn comes up to one parking orbit after the window, so the window may be that far in the past without
  -- the plan jumping to the next synodic period (which a time skip to just before the burn would otherwise trigger).
  local park = TAU * math.sqrt(rb ^ 3 / f.mu)
  local wait
  if math.abs(np - nt) > 1e-12 then wait = ((lead - phase) / (nt - np) + park) % (TAU / math.abs(np - nt)) - park end


  local vcirc = math.sqrt(f.mu / rb)
  local ecc = 1 + rb * vinf * vinf / f.mu
  local nu_inf = math.acos(-1 / ecc)
  local dv = math.sqrt(vinf * vinf + 2 * f.mu / rb) - vcirc
  local res = { kind = "ejection", vinf = vinf, dv = dv, tof = tof, lead = lead, phase = phase, wait = wait,
                outward = out > 0, c3 = vinf * vinf, nu_inf = nu_inf, again = park }
  local vdir = mul(dirp, out)
  res.plane = math.deg(math.asin(clamp(dot(vdir, o.W), -1, 1)))
  if wait then
    -- At the window the planet has moved on, so its velocity direction has turned with it.
    local vd = rotate(vdir, wp, np * wait)
    local inplane = unit(sub(vd, mul(o.W, dot(vd, o.W))))
    local burn_dir = rotate(inplane, o.W, -nu_inf)
    local nu_w = advance(o, o.nu, wait)
    local rw = state_at(o, nu_w)
    local ang = atan2(dot(cross(rw, burn_dir), o.W), dot(rw, burn_dir)) % TAU
    local to_burn = o.bound and ang / math.sqrt(f.mu / rb ^ 3) or 0
    res.t_burn = wait + to_burn
    if res.t_burn < 0 then res.t_burn = res.t_burn + park end
    local planet_dir = unit(rotate(dirp, wp, np * wait))
    res.eject_angle = math.deg(math.acos(clamp(dot(burn_dir, planet_dir), -1, 1)))
    res.eject_signed = atan2(dot(cross(planet_dir, burn_dir), o.W), dot(planet_dir, burn_dir))
    res.burn_dir, res.planet_dir, res.wp = burn_dir, planet_dir, wp
  end
  return res
end

-- Fast ejection: the flight time and the departure date are chosen (FLIGHT / DEPART with + and -) instead of taking the
-- Hohmann ones. A Lambert arc about the star joins the planet at departure to the target at arrival; the excess velocity
-- it needs over the planet's own gives the ejection, flown from the ship's orbit as in plan_ejection.

-- Departure excess speed of the Lambert arc that leaves the ship's planet T seconds from now and arrives tof seconds later.
local function fast_vinf(catalog, pidx, f, tidx, mu, T, tof)
  local c1, c2 = {}, {}
  local planet_p, planet_v = body_at(catalog, f.idx, T, c1)
  local sun_1, sun_v = body_at(catalog, pidx, T, c1)
  local r1, pv = sub(planet_p, sun_1), sub(planet_v, sun_v)
  local r2 = sub(body_at(catalog, tidx, T + tof, c2), (body_at(catalog, pidx, T + tof, c2)))
  local v1 = lambert_velocity(r1, r2, tof, mu, unit(cross(r1, pv)))
  return v1 and len(sub(v1, pv)) or nil
end

-- Porkchop search: the departure date and flight time (no longer than `max_days`) that need the least departure v-infinity
-- over the next synodic period. A coarse grid picks the valley and a finer grid around it settles the answer.
local function search_fast(f, s, max_days)
  local catalog = body_catalog()
  local pidx, tidx = body_by_name(f.parent), body_by_name(target_name())
  if not pidx or not tidx then return nil end
  local po, to = catalog[f.idx + 1].orbit, catalog[tidx + 1].orbit
  if not po or not to or not po.n or not to.n then return nil end
  local beat = math.abs(po.n - to.n)
  local span = math.min(beat > 1e-12 and TAU / beat or 800 * DAY, 800 * DAY)
  local hohmann = PI * math.sqrt(((po.a + to.a) / 2) ^ 3 / s.mu) / DAY
  local tof_hi = math.max(max_days, 20)
  local tof_lo = math.min(0.3 * hohmann, tof_hi * 0.5)
  local best
  local function try(d, t)
    local v = fast_vinf(catalog, pidx, f, tidx, s.mu, d * DAY, t * DAY)
    if v and (not best or v < best.vinf) then best = { vinf = v, depart = d, tof = t } end
  end
  local nd, nt = 40, 20
  for i = 0, nd do
    for j = 0, nt do try(0.2 + (span / DAY - 0.2) * i / nd, tof_lo + (tof_hi - tof_lo) * j / nt) end
  end
  if not best then return nil end
  local cd, ct = (span / DAY) / nd, (tof_hi - tof_lo) / nt
  local d0, t0 = best.depart, best.tof
  for i = -5, 5 do
    for j = -5, 5 do
      local d, t = d0 + cd * i / 5, t0 + ct * j / 5
      if d >= 0.2 and t >= 10 and t <= tof_hi then try(d, t) end
    end
  end
  return best
end

local function plan_ejection_fast(f, s)
  local catalog = body_catalog()
  local pidx, tidx = body_by_name(f.parent), body_by_name(target_name())
  if not pidx or not tidx then return nil end
  local now = props.get("/sim/time")
  local T, tof = fast.depart_at - now, fast.tof * DAY
  if T < 0 or tof < 5 * DAY then return nil end
  local c1, c2 = {}, {}
  local planet_p, planet_v = body_at(catalog, f.idx, T, c1)
  local sun_1 = body_at(catalog, pidx, T, c1)
  local target_p = body_at(catalog, tidx, T + tof, c2)
  local sun_2 = body_at(catalog, pidx, T + tof, c2)
  local sun_v = select(2, body_at(catalog, pidx, T, c1))
  local r1, r2, pv = sub(planet_p, sun_1), sub(target_p, sun_2), sub(planet_v, sun_v)
  local v1 = lambert_velocity(r1, r2, tof, s.mu, unit(cross(r1, pv)))
  if not v1 then return nil end
  local transfer = make_orbit(r1, v1, s.mu)
  if not transfer then return nil end

  local o = f.o
  local vinf_vec = sub(v1, pv)
  local vinf = len(vinf_vec)
  local rb = len(f.r)
  local vdir = unit(vinf_vec)
  local inplane = unit(sub(vdir, mul(o.W, dot(vdir, o.W))))
  local ecc = 1 + rb * vinf * vinf / f.mu
  local nu_inf = math.acos(-1 / ecc)
  local burn_dir = rotate(inplane, o.W, -nu_inf)
  local vb = math.sqrt(vinf * vinf + 2 * f.mu / rb)
  local dv = vb - math.sqrt(f.mu / rb)
  -- The burn comes ahead of the departure by the climb out of the sphere of influence.
  local hyper = make_orbit(mul(burn_dir, rb), mul(unit(cross(o.W, burn_dir)), vb), f.mu)
  local soi = catalog[f.idx + 1].soi
  local climb = 0
  if hyper and soi and soi > rb then
    climb = time_to(hyper, hyper.nu, math.acos(clamp((hyper.p / soi - 1) / hyper.e, -1, 1))) or 0
  end
  local tref = math.max(T - climb, 0)
  local rw = state_at(o, advance(o, o.nu, tref))
  local ang = atan2(dot(cross(rw, burn_dir), o.W), dot(rw, burn_dir)) % TAU
  local t_burn = tref + (o.bound and ang / math.sqrt(f.mu / rb ^ 3) or 0)
  local planet_dir = unit(pv)
  local arrive_v = select(2, state_at(transfer, advance(transfer, transfer.nu, tof)))
  local target_v = select(2, body_at(catalog, tidx, T + tof, c2))
  local tvel = sub(target_v, select(2, body_at(catalog, pidx, T + tof, c2)))
  return { kind = "ejection", fast = true, vinf = vinf, dv = dv, tof = tof, t_burn = t_burn, wait = T, c3 = vinf * vinf,
           outward = len(r2) > len(r1), nu_inf = nu_inf, again = TAU * math.sqrt(rb ^ 3 / f.mu),
           plane = math.deg(math.asin(clamp(dot(vdir, o.W), -1, 1))), arrive_vinf = len(sub(arrive_v, tvel)),
           eject_angle = math.deg(math.acos(clamp(dot(burn_dir, planet_dir), -1, 1))),
           eject_signed = atan2(dot(cross(planet_dir, burn_dir), o.W), dot(planet_dir, burn_dir)),
           transfer = transfer, planet_orbit = catalog[f.idx + 1].orbit, target_orbit = catalog[tidx + 1].orbit,
           planet_at = r1, target_at = r2 }
end

-- Return from the ship's orbit to the body its own body orbits: leave against the body's motion so the path falls to a
-- periapsis 300 km above the surface. There is no window; the burn is when the ship reaches the ejection point.
local function plan_return(f, s)
  local o = f.o
  local r1 = len(s.rm)
  local rp = s.radius + 3.0e5
  local a = (r1 + rp) / 2
  local vm = len(s.vm)
  local vinf = math.max(vm - math.sqrt(s.mu * (2 / r1 - 1 / a)), 1)
  local tof = PI * math.sqrt(a ^ 3 / s.mu)
  local rb = len(f.r)
  local ecc = 1 + rb * vinf * vinf / f.mu
  local nu_inf = math.acos(-1 / ecc)
  local dv = math.sqrt(vinf * vinf + 2 * f.mu / rb) - math.sqrt(f.mu / rb)
  local vdir = mul(unit(s.vm), -1)
  local inplane = unit(sub(vdir, mul(o.W, dot(vdir, o.W))))
  local burn_dir = rotate(inplane, o.W, -nu_inf)
  local ang = atan2(dot(cross(f.r, burn_dir), o.W), dot(f.r, burn_dir)) % TAU
  local t_burn = o.bound and ang / math.sqrt(f.mu / rb ^ 3) or 0
  return { kind = "return", vinf = vinf, dv = dv, tof = tof, t_burn = t_burn, rp = rp, again = TAU * math.sqrt(rb ^ 3 / f.mu),
           plane = math.deg(math.asin(clamp(dot(vdir, o.W), -1, 1))) }
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
  -- With the ship's own orbit drawn, the orbit stays fixed (periapsis to the right) and the ship travels along it, so
  -- the ship and the target both visibly move; otherwise the ship sits at angle 0 and the target is shown relative to it.
  local w = 0
  local me = actual and math.rad(so.nu) or 0
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
  local tx, ty = cx + d2 * math.cos(me + phase), cy - d2 * math.sin(me + phase)
  g:disc(tx, ty, 4)
  g:text(tx + 7, ty - 12, to_name, 0)
  g:colour(255, 255, 255)
  local sx, sy = cx + d1 * math.cos(me), cy - d1 * math.sin(me)
  g:disc(sx, sy, 4)
  g:text(sx + 7, sy - 12, from_name, 0)
end

-- The burn autopilot, armed from the plan on screen. While it waits, the start time is refreshed from the live plan each
-- frame this page draws, so the drift of the circular-orbit assumption over a long wait does not move the burn.
local function auto_apply(pl)
  local centre = pl.kind == "hohmann" and pl.wait or pl.t_burn
  if not centre then return nil end
  if pl.kind == "hohmann" then
    props.set("/autopilot/burn/mode", "radius")
    props.set("/autopilot/burn/target-radius", pl.r2)
  else
    props.set("/autopilot/burn/mode", "escape")
    props.set("/autopilot/burn/target-vinf", pl.vinf)
  end
  props.set("/autopilot/burn/start", props.get("/sim/time") + centre)
  return centre
end

local function auto_arm(pl)
  if pl.kind == "lambert" then return end
  local dv = math.abs((pl.kind == "hohmann" and pl.dv1 or pl.dv) or 0)
  if dv_short(dv) then return end
  local dur = burn_time(dv)
  if not dur or dv <= 0 then return end
  local centre = auto_apply(pl)
  if not centre then return end
  -- Too close to leave time for the pre-burn turn: wait for the next pass.
  if centre < dur / 2 + 60 and pl.again then
    props.set("/autopilot/burn/start", props.get("/sim/time") + centre + pl.again)
  end
  props.set("/autopilot/burn/state", "wait")
  props.set("/autopilot/burn/armed", true)
  auto_on = true
end

local function auto_abort()
  props.set("/autopilot/burn/armed", false)
  props.set("/autopilot/burn/state", "")
  props.set("/controls/throttle", 0)
  auto_on = false
end

local function auto_status(g, y)
  if not auto_on then return end
  g:colour(110, 220, 255)
  if props.get("/autopilot/burn/state") == "burn" then
    g:text(LX, y, string.format("AUTO burning  dV left %.0f m/s", props.get("/autopilot/burn/remaining")), 0)
  else
    g:text(LX, y, "AUTO armed  ignition in " .. fmt_time(props.get("/autopilot/burn/ignition-in")), 0)
  end
end

-- Once the burn has taken the ship out of a bound orbit there is no orbit to plan from; what is left to do is reach the
-- planned excess speed, which depends only on the speed and radius now.
local function draw_escape(g, f, vinf)
  local need = math.sqrt(vinf * vinf + 2 * f.mu / len(f.r))
  local left = math.max(need - len(f.v), 0)
  local now = f.o.bound and 0 or math.sqrt(-f.o.mu / f.o.a)
  g:text(LX, 62, "Ejection to " .. target_name(), 0)
  g:text(LX, 84, string.format("Target v inf  %.0f m/s", vinf), 0)
  g:text(LX, 102, f.o.bound and "Still bound" or string.format("Now v inf     %.0f m/s", now), 0)
  g:colour(255, 200, 80)
  if left < 1 then
    g:text(LX, 138, "Escape speed reached - cut the engines", 0)
  else
    local t = burn_time(left)
    g:text(LX, 138, string.format("dV left  %.0f m/s", left), 0)
    g:text(LX, 156, "Burn left  " .. (t and fmt_time(t) or "--"), 0)
  end
  auto_status(g, 190)
end

-- The Lambert arc about the star with the planet at departure and the target at arrival.
local function draw_fast_diagram(g, pl, cx, cy, R)
  local po, to, tr = pl.planet_orbit, pl.target_orbit, pl.transfer
  local P, Q = po.P, po.Q
  local extent = math.max(po.ra or po.rp * 2, to.ra or to.rp * 2, tr.ra or tr.rp * 2)
  local scale = R / extent
  local limit = R / scale * 1.5
  g:colour(255, 220, 90)
  g:disc(cx, cy, 3)
  g:colour(60, 110, 255)
  draw_orbit(g, po, P, Q, cx, cy, scale, limit)
  g:colour(255, 140, 60)
  draw_orbit(g, to, P, Q, cx, cy, scale, limit)
  g:colour(255, 255, 255)
  draw_orbit(g, tr, P, Q, cx, cy, scale, limit)
  g:colour(110, 170, 255)
  plot_point(g, pl.planet_at, P, Q, cx, cy, scale, 4, R + 4)
  g:colour(255, 140, 60)
  plot_point(g, pl.target_at, P, Q, cx, cy, scale, 4, R + 4)
end

local function draw_plan(g, f, ot, tr, tv)
  title(g, f)
  if not props.get("/target/valid") then need_target(g, 200); return nil end
  g:colour(255, 140, 60)
  g:text(200, 34, "Target: " .. target_name(), 1)
  g:colour(120, 255, 140)
  local plan
  local sib = (not ot) and sibling_state(f) or nil
  if sib and not f.o.bound then
    draw_escape(g, f, ejection_vinf(sib))
    return nil
  end
  local par = (not ot and not sib) and parent_state(f) or nil
  fast_ok = false
  if ot then
    local plane = math.deg(math.acos(clamp(dot(ot.W, f.o.W), -1, 1)))
    if f.o.e > 0.05 or ot.e > 0.05 or plane > 2 then
      plan = plan_lambert(f, ot)
    else
      plan = plan_hohmann(f, ot, tr, tv)
    end
  elseif sib then
    plan = plan_ejection(f, sib)
    fast_ok = f.o.bound
    if fast.target ~= target_name() then fast.mode, fast.target, fast.depart_at = 0, target_name(), nil end
    fast.failed = false
    M.sib, M.frame = sib, f
    if fast.pending_find then
      fast.pending_find = false
      local best = search_fast(f, sib, fast.tof)
      if best then fast.tof, fast.depart_at, fast.found = best.tof, props.get("/sim/time") + best.depart * DAY, true
      else fast.found = false end
    end
    if plan and fast.mode > 0 and fast.depart_at then
      local fp = plan_ejection_fast(f, sib)
      if fp then plan = fp else fast.failed = true end
    end
  elseif par then plan = plan_return(f, par) end
  if not plan then
    g:colour(255, 200, 80)
    g:text(200, 190, "No transfer plan for this target", 1)
    if props.get("/target/kind") == "ship" then
      g:text(200, 168, "A ship in another body's orbit; for a planet", 1)
      g:text(200, 180, "press TGT+ on past the ships to the planet", 1)
    end
    g:text(200, 212, "Pick a body orbiting " .. f.soi, 1)
    if f.parent and f.parent ~= "" then g:text(200, 230, "or " .. f.parent .. " itself, or another body of " .. f.parent, 1) end
    return nil
  end
  if not f.o.bound then
    g:colour(255, 200, 80)
    g:text(200, 200, "Needs a bound orbit", 1)
    return nil
  end
  if fast.failed then
    g:colour(255, 120, 80)
    g:text(LX, 36, "No flight for that time; Hohmann shown", 0)
  end
  if plan.kind == "return" then
    g:text(LX, 62, "Return to " .. target_name(), 0)
    dv_warning(g, plan.dv)
    g:text(LX, 84, string.format("Departure dV  %.1f m/s", plan.dv), 0)
    g:text(LX, 102, string.format("v inf %.0f m/s", plan.vinf), 0)
    g:text(LX, 120, "Transfer time  " .. fmt_time(plan.tof), 0)
    g:text(LX, 138, string.format("Arrival periapsis %.0f km", (plan.rp - props.get("/universe/bodies/body[" .. body_by_name(target_name()) .. "]/radius")) / 1000), 0)
    g:colour(255, 200, 80)
    g:text(LX, 164, "Departure burn in  " .. fmt_time(plan.t_burn), 0)
    g:text(LX, 182, "Burn prograde (set the node, then fly it)", 0)
    if not burn_left_line(g, LX, 218, plan.dv) then g:text(LX, 218, "Burn time  " .. fmt_time(burn_time(plan.dv)), 0) end
    auto_status(g, 254)
    if math.abs(plan.plane) > 0.5 then
      g:colour(255, 120, 80)
      g:text(LX, 200, string.format("Orbit plane is %.1f deg off", math.abs(plan.plane)), 0)
    end
  elseif plan.kind == "lambert" then
    g:text(LX, 62, "Lambert intercept", 0)
    dv_warning(g, plan.dv)
    g:text(LX, 84, string.format("Departure dV  %.1f m/s", plan.dv), 0)
    g:text(LX, 102, string.format("Pro %+7.1f  Rad %+7.1f m/s", plan.components[1], plan.components[2]), 0)
    g:text(LX, 120, string.format("Normal %+7.1f m/s", plan.components[3]), 0)
    g:text(LX, 146, "Flight time  " .. fmt_time(plan.tof), 0)
    g:colour(255, 200, 80)
    g:text(LX, 164, "Burn now; TO MAN loads the vector", 0)
    if not burn_left_line(g, LX, 200, plan.dv) then
      g:text(LX, 200, "Burn time  " .. (burn_time(plan.dv) and fmt_time(burn_time(plan.dv)) or "--"), 0)
    end
    g:colour(255, 120, 80)
    g:text(LX, 218, "Two-body intercept; no arrival capture burn", 0)
  elseif plan.kind == "hohmann" then
    g:text(LX, 62, "Hohmann transfer", 0)
    dv_warning(g, math.abs(plan.dv1))
    g:text(LX, 84, "Burn 1  " .. fmt_dv(plan.dv1), 0)
    g:text(LX, 102, "Burn 2  " .. fmt_dv(plan.dv2), 0)
    g:text(LX, 120, string.format("Total   %.1f m/s", math.abs(plan.dv1) + math.abs(plan.dv2)), 0)
    g:text(LX, 146, "Flight time  " .. fmt_time(plan.tof), 0)
    g:text(LX, 164, string.format("Phase now %.1f  need %.1f deg", math.deg(plan.phase), math.deg(plan.lead)), 0)
    g:colour(255, 200, 80)
    g:text(LX, 190, "Burn 1 in  " .. fmt_time(plan.wait), 0)
    if not burn_left_line(g, LX, 226, plan.dv1) then g:text(LX, 226, "Burn 1 time  " .. fmt_time(burn_time(plan.dv1)), 0) end
    auto_status(g, 244)
    if plan.plane > 0.5 then
      g:colour(255, 120, 80)
      g:text(LX, 208, string.format("Planes differ by %.1f deg", plan.plane), 0)
    end
    transfer_diagram(g, 200, 318, 56, len(f.r), len(tr), plan.phase, plan.lead, f.soi, "You", target_name(),
      { a = ship("orbit/semi-major-axis"), e = ship("orbit/eccentricity"), nu = ship("orbit/true-anomaly") })
  elseif plan.fast then
    local now = props.get("/sim/time")
    g:text(LX, 62, "Fast ejection to " .. target_name(), 0)
    dv_warning(g, plan.dv)
    g:text(LX, 80, string.format("Ejection dV  %.1f m/s", plan.dv), 0)
    g:text(LX, 96, string.format("v inf %.0f m/s   C3 %.1f km2/s2", plan.vinf, plan.c3 / 1e6), 0)
    if fast.mode == 3 then
      g:colour(255, 240, 140)
      g:text(LX, 234, fast.found == false and "No flight found; raise FLIGHT" or "BEST: FIND picks the cheapest date", 0)
      g:colour(120, 255, 140)
    end
    if fast.mode == 2 then g:colour(255, 240, 140) end
    g:text(LX, 114, (fast.mode == 2 and ">" or " ") .. "Depart in  " .. fmt_time(plan.wait), 0)
    g:colour(120, 255, 140)
    if fast.mode == 1 or fast.mode == 3 then g:colour(255, 240, 140) end
    g:text(LX, 130, ((fast.mode == 1 or fast.mode == 3) and ">" or " ") .. string.format("Flight  %.0f days", plan.tof / DAY), 0)
    g:colour(120, 255, 140)
    g:text(LX, 146, string.format("Arrival v inf %.0f m/s", plan.arrive_vinf), 0)
    g:colour(255, 200, 80)
    g:text(LX, 164, "Ejection burn in  " .. fmt_time(plan.t_burn), 0)
    if not burn_left_line(g, LX, 198, plan.dv) then g:text(LX, 198, "Burn time  " .. fmt_time(burn_time(plan.dv)), 0) end
    auto_status(g, 216)
    if math.abs(plan.plane) > 0.5 then
      g:colour(255, 120, 80)
      g:text(LX, 182, string.format("Orbit plane is %.1f deg off", math.abs(plan.plane)), 0)
    end
    draw_fast_diagram(g, plan, 200, 312, 72)
  else
    g:text(LX, 62, plan.outward and "Ejection to an outer planet" or "Ejection to an inner planet", 0)
    dv_warning(g, plan.dv)
    g:text(LX, 84, string.format("Ejection dV  %.1f m/s", plan.dv), 0)
    g:text(LX, 102, string.format("v inf %.0f m/s   C3 %.2f km2/s2", plan.vinf, plan.c3 / 1e6), 0)
    g:text(LX, 120, "Transfer time  " .. fmt_time(plan.tof), 0)
    g:text(LX, 138, string.format("Phase now %.1f  need %.1f deg", math.deg(plan.phase), math.deg(plan.lead)), 0)
    g:colour(255, 200, 80)
    g:text(LX, 164, "Window in  " .. fmt_time(plan.wait), 0)
    g:text(LX, 182, "Ejection burn in  " .. fmt_time(plan.t_burn), 0)
    if not burn_left_line(g, LX, 236, plan.dv) then g:text(LX, 236, "Burn time  " .. fmt_time(burn_time(plan.dv)), 0) end
    auto_status(g, 254)
    if plan.eject_angle then g:text(LX, 200, string.format("Ejection angle %.1f deg from %s", plan.eject_angle, plan.outward and "prograde" or "retrograde"), 0) end
    if math.abs(plan.plane) > 0.5 then
      g:colour(255, 120, 80)
      g:text(LX, 218, string.format("Orbit plane is %.1f deg off", math.abs(plan.plane)), 0)
    end
    transfer_diagram(g, 120, 322, 50, len(sib.rp), len(sib.rt), plan.phase, plan.lead, f.parent, f.soi, target_name())
    -- The planet's path, with the burn point at the ejection angle from its direction of travel.
    local cx, cy, r = 300, 320, 45
    g:colour(60, 110, 255)
    g:circle(cx, cy, r)
    g:colour(255, 255, 255)
    g:line(cx, cy - r - 24, cx, cy - r)
    g:tri(cx, cy - r - 28, cx - 4, cy - r - 18, cx + 4, cy - r - 18)
    g:text(cx + 8, cy - r - 30, plan.outward and "pro" or "retro", 0)
    if plan.eject_angle then
      -- Screen-up is the planet's direction of travel and the orbit normal points at the viewer.
      local ang = plan.eject_signed
      g:colour(255, 200, 80)
      g:disc(cx - math.sin(ang) * r, cy - math.cos(ang) * r, 4)
    end
  end
  return plan
end

function M.init(self) end

-- Scripts (and --set) can drive the planner: set /orbitcomp/page to 1-4 and the node values, and it is applied once.
-- Setting /orbitcomp/auto to 1 while the PLN page shows a plan arms the burn autopilot, as the AUTO button does.
local function apply_script()
  local p = tonumber(props.get("/orbitcomp/page")) or 0
  if p < 1 then return end
  page = clamp(math.floor(p), 1, 4)
  node.mode = clamp(math.floor(tonumber(props.get("/orbitcomp/node-mode")) or 1), 1, #NODE_MODES)
  node.time = tonumber(props.get("/orbitcomp/time")) or node.time
  node.dv = { tonumber(props.get("/orbitcomp/prograde")) or 0, tonumber(props.get("/orbitcomp/radial")) or 0,
              tonumber(props.get("/orbitcomp/normal")) or 0 }
  -- The fast ejection can be set the same way: /orbitcomp/fast (1-3), /orbitcomp/flight-days and /orbitcomp/depart-days.
  local fm = tonumber(props.get("/orbitcomp/fast"))
  if fm and fm > 0 then
    fast.mode = clamp(math.floor(fm), 1, 3)
    fast.target = target_name()
    fast.tof = tonumber(props.get("/orbitcomp/flight-days")) or 500
    fast.depart_at = props.get("/sim/time") + (tonumber(props.get("/orbitcomp/depart-days")) or 1) * DAY
    fast.pending_find = fast.mode == 3 and props.get("/orbitcomp/find") == 1
    props.set("/orbitcomp/find", 0)
    props.set("/orbitcomp/fast", 0)
  end
  props.set("/orbitcomp/page", 0)
end

function M.draw(self, g, w, h)
  frame_count = frame_count + 1
  props.set("/plan/orbitcomp/stamp", -1)
  track_burn()
  apply_script()
  local f = ship("landed") and nil or ship_frame()
  if not f then
    g:colour(255, 240, 140)
    g:text(200, 14, "ORBITCOMP", 1)
    g:colour(255, 200, 80)
    g:text(200, 200, "No orbit", 1)
    M.plan = nil
    set_buttons()
    return
  end
  local ot, tr, tv = target_state(f)
  M.plan = nil
  if page == 1 then draw_orb(g, f, ot, tr)
  elseif page == 2 then draw_man(g, f, ot, tr)
  elseif page == 4 then draw_hel(g, f)
  else
    M.plan = draw_plan(g, f, ot, tr, tv)
    local pl = M.plan
    if pl and props.get("/orbitcomp/auto") == 1 then
      props.set("/orbitcomp/auto", 0)
      auto_arm(pl)
    end
    if auto_on then
      if not props.get("/autopilot/burn/armed") then auto_on = false
      elseif pl and props.get("/autopilot/burn/state") == "wait" then auto_apply(pl) end
    end
    if pl then
      local centre = pl.kind == "lambert" and (burn_time(pl.dv) or 0) / 2
        or (pl.kind == "hohmann" and pl.wait or pl.t_burn)
      local dv = math.abs((pl.kind == "hohmann" and pl.dv1 or pl.dv) or 0)
      if centre and dv > 0 then publish_burn(centre, burn_time(dv), pl.kind .. " transfer") end
    end
  end
  set_buttons()
end

function M.button(self, i)
  scan_cache = nil
  if i <= 3 then page = i; return end
  if i == 6 then page = 4; return end
  if i == 4 then cycle_target(1); return end
  if i == 5 then cycle_target(-1); return end
  if page == 1 or page == 4 then
    if i == 7 then zoom = math.min(zoom * 1.5, 40) elseif i == 8 then zoom = math.max(zoom / 1.5, 0.1) elseif i == 9 then zoom = 1 end
  elseif page == 2 then
    local sgn = 0
    if i == 7 then node.field = node.field % 4 + 1
    elseif i == 8 then sgn = 1
    elseif i == 9 then sgn = -1
    elseif i == 10 then step = step % #DV_STEPS + 1
    elseif i == 11 then node.mode = node.mode % #NODE_MODES + 1
    elseif i == 12 then node.dv = { 0, 0, 0 }; node.time = 600 end
    if sgn ~= 0 then
      if node.field == 4 then
        node.time = math.max(0, node.time + sgn * TIME_STEPS[step])
        node.mode = 1
      else
        node.dv[node.field] = node.dv[node.field] + sgn * DV_STEPS[step]
      end
    end
  elseif i >= 10 and fast_ok then
    if i == 10 then
      fast.mode = (fast.mode + 1) % 4
      local p = M.plan
      if fast.mode == 0 then fast.depart_at = nil
      elseif not fast.depart_at and p then
        fast.tof = p.tof / DAY
        fast.depart_at = props.get("/sim/time") + math.max(p.t_burn or 0, 0.1 * DAY)
      end
    elseif not fast.depart_at then
      return
    elseif fast.mode == 3 then
      if i == 11 and M.sib and M.frame then
        local best = search_fast(M.frame, M.sib, fast.tof)
        if best then
          fast.tof, fast.depart_at, fast.found = best.tof, props.get("/sim/time") + best.depart * DAY, true
        else
          fast.found = false
        end
      end
    elseif fast.mode == 1 then
      fast.tof = clamp(fast.tof + (i == 11 and 10 or -10), 10, 5000)
    elseif fast.mode == 2 then
      fast.depart_at = math.max(fast.depart_at + (i == 11 and DAY or -DAY), props.get("/sim/time") + 0.1 * DAY)
    end
  elseif i == 8 then
    if M.plan then auto_arm(M.plan) end
  elseif i == 9 then
    auto_abort()
  elseif i == 7 and M.plan then
    local p = M.plan
    node.mode = 1
    node.field = 1
    if p.kind == "lambert" then
      node.time = 0
      node.dv = { p.components[1], p.components[2], p.components[3] }
    elseif p.kind == "hohmann" then
      node.time = math.max(p.wait or 0, 0)
      node.dv = { p.dv1, 0, 0 }
    elseif p.kind == "return" then
      node.time = math.max(p.t_burn or 0, 0)
      node.dv = { p.dv, 0, 0 }
    else
      node.time = math.max(p.t_burn or 0, 0)
      node.dv = { p.dv, 0, 0 }
    end
    page = 2
  end
end

M._test = { make_orbit = make_orbit, advance = advance, state_at = state_at, time_to = time_to, closest = closest,
            plan_ejection = plan_ejection, plan_hohmann = plan_hohmann, plan_lambert = plan_lambert,
            lambert_velocity = lambert_velocity, mean_from_nu = mean_from_nu, nu_from_mean = nu_from_mean,
            plan_ejection_fast = plan_ejection_fast, body_catalog = body_catalog, body_at = body_at, patched_encounter = patched_encounter }

return M
