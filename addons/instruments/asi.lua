-- Airspeed indicator: one needle sweeping 300 degrees over 0 to 500 knots (asi_face.png, needle_long.png).

local MAX_KT = 500

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Airspeed",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h, bare)
    local kt = ship("speed") * 1.943844
    g:image("asi_face.png", 200, 190, 300, 300)
    local shown = math.max(0, math.min(kt, MAX_KT * 1.04))
    g:image("needle_long.png", 200, 190, 300, 300, -150 + shown / MAX_KT * 300)
    if not bare then
      g:colour(120, 255, 140)
      g:text(200, 378, string.format("IAS %.0f kt", kt), 1)
    end
  end,

  button = function(self, i) end,
}
