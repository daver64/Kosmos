-- DG Propellant: the Orbiter panel region is the background; values are drawn over it.
local S, K = 117, 400 / 117
local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

return {
  name = "DG Propellant",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    g:image("panel/panel1.png", 200, 200, 400, 400, 0, 27, 81, 117, 117)
    local fuel = ship("stage-fuel")
    local q = math.max(0, math.min(1, fuel / 11400))
    g:colour(60, 255, 90)
    g:fillrect(74 * K, (62 - 38 * q) * K, 84 * K, 62 * K)
  end,
  button = function(self, i) end,
}
