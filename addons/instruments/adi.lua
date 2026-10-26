-- Attitude indicator: the classic blue-and-brown ball, drawn from the bitmaps in this folder.
-- adi_ball.png is a tall strip of sky and ground (3 px per degree of pitch, pitch 0 on the middle row); the script cuts a 280 px
-- window out of it at the current pitch and turns it by the roll. adi_bezel.png masks that window round and carries the ring,
-- roll scale and aircraft symbol; adi_pointer.png is the roll pointer. Replace any of them with finished artwork of the same size.

local BALL, PX_PER_DEG, STRIP_H = 280, 3, 820

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Attitude Indicator",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h, bare)
    local pitch = math.max(-90, math.min(90, ship("pitch")))
    local roll = ship("roll")
    local sy = math.floor(STRIP_H / 2 - BALL / 2 - pitch * PX_PER_DEG + 0.5)
    g:image("adi_ball.png", 200, 200, BALL, BALL, -roll, 0, sy, BALL, BALL)
    g:image("adi_bezel.png", 200, 200, 400, 400)
    g:image("adi_pointer.png", 200, 200, 400, 400, -roll)

    g:colour(255, 240, 140)
    g:text(200, 22, "ATTITUDE", 1)
    if not bare then
      g:colour(120, 255, 140)
      g:text(70, 378, string.format("PIT %+.1f", pitch), 0)
      g:text(200, 378, string.format("HDG %03.0f", ship("heading")), 1)
      g:text(330, 378, string.format("ROL %+.1f", roll), 2)
    end
  end,

  button = function(self, i) end,
}
