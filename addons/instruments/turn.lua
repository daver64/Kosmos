-- Turn coordinator: the miniature aircraft banks with the turn rate (a 3 degree per second turn lines it up with the marks) and
-- the ball in the tube shows slip. The turn rate is the heading change, smoothed. Images: tc_face.png, tc_aircraft.png.

local TUBE_Y, TUBE_CURVE = 236, 100

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Turn Coordinator",
  buttons = {},
  init = function(self) self.rate = 0 end,

  update = function(self, dt)
    local hdg = ship("heading")
    if self.last and dt > 0 then
      local d = (hdg - self.last + 540) % 360 - 180
      self.rate = self.rate + (d / dt - self.rate) * math.min(1, dt * 5)
    end
    self.last = hdg
  end,

  draw = function(self, g, w, h, bare)
    local rate = self.rate or 0
    g:image("tc_face.png", 200, 190, 300, 300)
    g:image("tc_aircraft.png", 200, 190, 300, 300, math.max(-35, math.min(35, rate / 3 * 20)))
    local dx = math.max(-1, math.min(1, ship("sideslip") / 8)) * 56
    g:colour(235, 235, 240)
    g:disc(200 + dx, 190 + TUBE_Y - 150 - dx * dx / TUBE_CURVE, 9)
    if not bare then
      g:colour(120, 255, 140)
      g:text(200, 378, string.format("RATE %+.1f deg/s", rate), 1)
    end
  end,

  button = function(self, i) end,
}
