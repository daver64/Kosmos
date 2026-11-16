local values = {
  ["/sim/active-ship"] = 0,
  ["/ships/ship[0]/stage-fuel"] = 50,
  ["/ships/ship[0]/stage-fuel-capacity"] = 200,
}
props = { get = function(path) return values[path] end }

local mfd = dofile(root .. "/addons/instruments/engine.lua")
local fuel_width, fuel_colour, colour
local canvas = {
  colour = function(_, r, g, b) colour = {r, g, b} end,
  rect = function() end,
  fillrect = function(_, x1, y1, x2)
    if y1 == 115 then fuel_width, fuel_colour = x2 - x1, colour end
  end,
  text = function() end,
}
local function check(fraction, red)
  mfd:draw(canvas, 400, 400)
  assert(math.abs(fuel_width - 188 * fraction) < 1e-6)
  assert(fuel_colour[1] == (red and 255 or 120))
end

mfd:init()
mfd:update()
check(0.25, false)
values["/ships/ship[0]/stage-fuel"] = 20
check(0.1, true)
values["/ships/ship[0]/stage-fuel"] = 0
check(0, true)
values["/ships/ship[0]/stage-fuel"] = 200
check(1, false)
values["/ships/ship[0]/stage-fuel-capacity"] = 0
check(0, true)
values["/ships/ship[0]/stage-fuel"] = 30
values["/ships/ship[0]/stage-fuel-capacity"] = 100
check(0.3, false)
values["/sim/active-ship"] = 1
values["/ships/ship[1]/stage-fuel"] = 10
values["/ships/ship[1]/stage-fuel-capacity"] = 100
check(0.1, true)
