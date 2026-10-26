-- DG Slope: the Orbiter panel region is the background; values are drawn over it.
local S, K = 41, 400 / 41
local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

return {
  name = "DG Slope",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    g:image("panel/panel1.png", 200, 200, 400, 400, 0, 414, 405, 41, 41)
    local sl = math.max(-22, math.min(7, ship("flight-path")))
    g:colour(255, 255, 255)
    local y = (35 - (sl + 22) / 29 * 60) * K
    g:fillrect(10 * K, y - 1, 30 * K, y + 1)
    g:colour(255, 60, 60)
    g:text(40 * K, 20 * K, string.format("%.0f", ship("flight-path")), 0)
  end,
  button = function(self, i) end,
}
