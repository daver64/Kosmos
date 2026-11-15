local values = {
  ["/sim/active-ship"] = 0,
  ["/ships/ship[0]/systems/present"] = true,
  ["/ships/ship[0]/systems/bus/powered"] = true,
}
props = { get = function(path) return values[path] end }

local base = "/ships/ship[0]/systems/"
local mfd = dofile(root .. "/addons/stockmfd/systems.lua")
mfd:init()
local lines = {}
local canvas = {
  colour = function() end,
  rect = function() end,
  fillrect = function() end,
  text = function(_, _, y, text) lines[text] = y end,
}
local function draw()
  lines = {}
  mfd:draw(canvas, 400, 400)
end
local function component(i, kind, alive, output)
  local path = base .. "component[" .. i .. "]/"
  values[path .. "type"] = kind
  values[path .. "alive"] = alive
  values[path .. "output-w"] = output
end

values[base .. "component-count"] = 1
component(0, "solar", true, 0)
draw()
assert(lines["Solar 0 W"] and lines["BATT"] > lines["Solar 0 W"])

values[base .. "component-count"] = 3
component(0, "solar", true, 1200)
component(1, "solar", true, 345)
component(2, "rtg", true, 100)
draw()
assert(lines["Solar 1545 W"])

component(0, "solar", false, 1200)
draw()
assert(lines["Solar 345 W"])
component(1, "solar", false, 345)
draw()
assert(not lines["Solar 0 W"] and not lines["Solar 345 W"])
