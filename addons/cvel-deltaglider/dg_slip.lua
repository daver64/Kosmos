local function ship(k) return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. k) or 0 end

return {
  name = "DG slip angle",
  buttons = {},
  init = function(self) end,
  draw = function(self, g, w, h, bare)
    local value = ship("sideslip")
    local a = math.pi / 2 - math.rad(math.max(-165, math.min(165, 7.7 * value)))
    g:colour(18, 24, 30)
    g:fillrect(0, 0, 400, 400)
    g:colour(150, 170, 180)
    g:circle(200, 200, 180)
    g:text(200, 35, "SLIP", 1)
    g:text(200, 345, string.format("%.1f deg", value), 1)
    for deg = -20, 20, 10 do
      local tick = math.pi / 2 - math.rad(7.7 * deg)
      g:line(200 + 160 * math.cos(tick), 200 - 160 * math.sin(tick),
             200 + 180 * math.cos(tick), 200 - 180 * math.sin(tick))
    end
    g:colour(255, 255, 255)
    g:line(200, 200, 200 + 145 * math.cos(a), 200 - 145 * math.sin(a))
  end,
  button = function(self, i) end,
}
