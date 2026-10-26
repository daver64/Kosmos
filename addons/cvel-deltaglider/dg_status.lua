-- DG Engine status: the Orbiter panel region is the background; values are drawn over it.
local S, K = 112, 400 / 166
local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

return {
  name = "DG Engine status",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    g:image("panel/panel1.png", 200, 200, 400, 400, 0, 1000, 90, 112, 112)
    local thr = ship("thrust")
    g:colour(60, 255, 90)
    local lvl = props.get("/controls/throttle") or 0
    local hov = props.get("/controls/hover") or 0
    g:fillrect(27 * K, (60 - 40 * lvl) * K, 48 * K, 60 * K)
    g:fillrect(76 * K, (60 - 40 * hov) * K, 86 * K, 60 * K)
  end,
  button = function(self, i) end,
}
