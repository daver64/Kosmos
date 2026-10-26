-- DG slip angle: the Orbiter panel region is blitted as the background, then the needle is drawn over it.
local S, K = 56, 400 / 56
local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

return {
  name = "DG slip angle",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    g:image("panel/panel1.png", 200, 200, 400, 400, 0, 305, 413, 56, 56)
    local a = math.pi / 2 - math.rad(math.max(-165, math.min(165, 7.7 * ship("sideslip"))))
    local L = 26 * K
    g:colour(255, 255, 255)
    g:line(200, 200, 200 + L * math.cos(a), 200 - L * math.sin(a))
    g:line(201, 200, 201 + L * math.cos(a), 200 - L * math.sin(a))
  end,
  button = function(self, i) end,
}
