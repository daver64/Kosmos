local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

local function scale_y(deg)
  deg = math.max(-22, math.min(7, deg))
  return 90 + (7 - deg) * 220 / 29
end

return {
  name = "DG Slope",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    local slope = ship("flight-path")
    local y = scale_y(slope)
    g:colour(18, 24, 30)
    g:fillrect(0, 0, 400, 400)
    g:colour(150, 170, 180)
    g:text(200, 35, "FLIGHT PATH", 1)
    g:line(100, 90, 100, 310)
    for _, deg in ipairs({ 5, 0, -5, -10, -20 }) do
      local tick = scale_y(deg)
      g:line(100, tick, 120, tick)
      g:text(130, tick, tostring(deg), 0)
    end
    g:colour(255, 90, 60)
    g:fillrect(82, y - 3, 118, y + 3)
    g:colour(60, 255, 90)
    g:text(275, 200, string.format("%.1f deg", slope), 1)
  end,
  button = function(self, i) end,
}
