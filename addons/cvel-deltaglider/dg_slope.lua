-- DG descent slope: the whole Orbiter instrument (scale and readout) is blitted as the background, a marker rides the scale.
local SX, SY, S = 389, 360, 161
local K = 400 / S
local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

-- Panel rows of the scale marks, for slopes in degrees.
local MARKS = { { 7, 411 }, { 5, 413 }, { 0, 423 }, { -5, 435 }, { -10, 447 }, { -20, 468 }, { -22, 472 } }

local function scale_y(deg)
  deg = math.max(-22, math.min(7, deg))
  for i = 1, #MARKS - 1 do
    local a, b = MARKS[i], MARKS[i + 1]
    if deg <= a[1] and deg >= b[1] then
      return a[2] + (b[2] - a[2]) * (a[1] - deg) / (a[1] - b[1])
    end
  end
  return MARKS[#MARKS][2]
end

return {
  name = "DG Slope",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    g:image("panel/panel1.png", 200, 200, 400, 400, 0, SX, SY, S, S)
    local slope = ship("flight-path")
    local y = (scale_y(slope) - SY) * K
    g:colour(255, 90, 60)
    g:fillrect((407 - SX) * K, y - 1, (414 - SX) * K, y + 1)
    g:colour(60, 255, 90)
    g:text((528 - SX) * K, (423 - SY) * K, string.format("%.1f", slope), 2)
  end,
  button = function(self, i) end,
}
