-- Heading indicator: the compass card (dg_card.png) turns under a fixed aircraft symbol and lubber line (dg_face.png).

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Heading",
  buttons = {},
  init = function(self) end,

  draw = function(self, g, w, h, bare)
    local hdg = ship("heading")
    g:image("dg_card.png", 200, 190, 300, 300, -hdg)
    g:image("dg_face.png", 200, 190, 300, 300)
    if not bare then
      g:colour(120, 255, 140)
      g:text(200, 378, string.format("HDG %03.0f", hdg), 1)
    end
  end,

  button = function(self, i) end,
}
