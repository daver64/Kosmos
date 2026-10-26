-- Vertical speed indicator: zero at 9 o'clock, 3000 ft/min either way (vsi_face.png, needle_long.png).

local MAX_FPM = 3000

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Vertical Speed",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h, bare)
    local fpm = ship("vertical-speed") * 196.8504
    g:image("vsi_face.png", 200, 190, 300, 300)
    local shown = math.max(-MAX_FPM, math.min(fpm, MAX_FPM))
    g:image("needle_long.png", 200, 190, 300, 300, -90 + shown / MAX_FPM * 170)
    if not bare then
      g:colour(120, 255, 140)
      g:text(200, 378, string.format("VS %+.0f fpm", fpm), 1)
    end
  end,

  button = function(self, i) end,
}
