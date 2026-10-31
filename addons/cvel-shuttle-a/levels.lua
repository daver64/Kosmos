-- Shuttle-A engine level bars: the four green slots on the Orbiter panel, filled from the bottom. The main engines follow the
-- throttle (Z/X, Shift/Ctrl) and the hover engines follow the hover control (keypad + and -, 0, .).

local bars = {
  { x = 46, key = "/controls/throttle", label = "MAIN L" },
  { x = 126, key = "/controls/throttle", label = "MAIN R" },
  { x = 206, key = "/controls/hover", label = "HOVER F" },
  { x = 286, key = "/controls/hover", label = "HOVER A" },
}

return {
  name = "Engine levels",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h, bare)
    g:text(200, 22, "ENGINE LEVELS", 1)
    for _, b in ipairs(bars) do
      local level = math.max(0, math.min(1, props.get(b.key)))
      g:colour(40, 120, 60)
      g:rect(b.x, 44, b.x + 60, 344)
      if level > 0.002 then
        g:colour(60, 255, 90)
        g:fillrect(b.x + 2, 344 - 300 * level, b.x + 58, 344)
      end
      g:colour(60, 255, 90)
      g:text(b.x + 30, 366, b.label, 1)
    end
  end,

  button = function(self, i) end,
}
