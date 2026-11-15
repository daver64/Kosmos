local values = {
  ["/sim/active-ship"] = 0,
  ["/sim/time"] = 100,
  ["/universe/bodies/count"] = 0,
  ["/ships/ship[0]/landed"] = true,
  ["/ships/ship[0]/mass"] = 1000,
  ["/ships/ship[0]/thrust"] = 0,
  ["/ships/ship[0]/main-max-thrust"] = 10000,
  ["/ships/ship[0]/engine-delta-v/x"] = 0,
  ["/ships/ship[0]/engine-delta-v/y"] = 0,
  ["/ships/ship[0]/engine-delta-v/z"] = 0,
  ["/orbitcomp/page"] = 2,
  ["/orbitcomp/time"] = 600,
  ["/orbitcomp/prograde"] = 1000,
}
props = {
  get = function(path) return values[path] end,
  set = function(path, value) values[path] = value end,
}
local mfd = dofile(root .. "/addons/stockmfd/orbitcomp.lua")
local g = { colour = function() end, text = function() end }
local hooks = mfd._test
local mu = 3.986004418e14
local r, v = {7000000, 0, 0}, {0, math.sqrt(mu / 7000000), 0}
local function frame(velocity)
  return { o = hooks.make_orbit(r, velocity, mu), mu = mu }
end
local function close(a, b) assert(math.abs(a - b) < 1e-5, a .. " != " .. b) end

mfd:draw(g, 400, 400)
local node = hooks.compute_node(frame(v))
close(node.t, 600)
close(node.dv, 1000)
values["/sim/time"] = 250
node = hooks.compute_node(frame(v))
close(node.t, 450)
values["/sim/time"] = 900
node = hooks.compute_node(frame(v))
close(node.t, 0)

values["/ships/ship[0]/engine-delta-v/y"] = 400
node = hooks.compute_node(frame({0, v[2] + 400, 0}))
assert(node.started)
close(node.t, 0)
close(node.dvv[2], 600)
close(node.dv, 600)
local expected_after = hooks.make_orbit(r, {0, v[2] + 1000, 0}, mu)
close(node.after.e, expected_after.e)
close(node.after.ra, expected_after.ra)
values["/ships/ship[0]/engine-delta-v/y"] = 1000
node = hooks.compute_node(frame({0, v[2] + 1000, 0}))
close(node.dv, 0)
close(node.after.ra, expected_after.ra)

-- A sideways or excessive burn leaves a correction vector, rather than
-- pretending the original manoeuvre was completed.
values["/ships/ship[0]/engine-delta-v/x"] = 200
values["/ships/ship[0]/engine-delta-v/y"] = 1200
node = hooks.compute_node(frame({200, v[2] + 1200, 0}))
close(node.dvv[1], -200)
close(node.dvv[2], -200)
close(node.dv, math.sqrt(80000))
local paused = hooks.compute_node(frame({200, v[2] + 1200, 0}))
close(paused.dv, node.dv)

-- Once started, progress remains latched even if subsequent thrust cancels it.
values["/ships/ship[0]/engine-delta-v/x"] = 0
values["/ships/ship[0]/engine-delta-v/y"] = 0
node = hooks.compute_node(frame(v))
assert(node.started)
close(node.dv, 1000)

values["/orbitcomp/page"] = 2
values["/orbitcomp/time"] = 30
values["/orbitcomp/prograde"] = 50
mfd:draw(g, 400, 400)
node = hooks.compute_node(frame(v))
assert(not node.started)
close(node.t, 30)
close(node.dv, 50)

values["/sim/active-ship"] = 1
values["/ships/ship[1]/landed"] = true
values["/ships/ship[1]/mass"] = 1000
values["/ships/ship[1]/thrust"] = 0
values["/ships/ship[1]/main-max-thrust"] = 10000
mfd:draw(g, 400, 400)
node = hooks.compute_node(frame(v))
assert(not node.started)
close(node.t, 600)
close(node.dv, 0)
