-- Shuttle-A engine level bars: the four green slots on the Orbiter panel, filled from the bottom. The main engines follow the
-- throttle (Z/X, Shift/Ctrl) and the hover engines follow the hover control (keypad + and -, 0, .).

local bars = {
  { x = 46, key = "/controls/throttle" },
  { x = 96, key = "/controls/throttle" },
  { x = 226, key = "/controls/hover" },
  { x = 276, key = "/controls/hover" },
}

return {
  name = "Engine levels",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h, bare)
    for _, b in ipairs(bars) do
      local level = math.max(0, math.min(1, props.get(b.key)))
      if level > 0.002 then
        g:colour(60, 255, 90)
        g:fillrect(b.x, 384 - 360 * level, b.x + 20, 384)
      end
    end
  end,

  button = function(self, i) end,
}
