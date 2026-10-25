-- Align Planes MFD: relative inclination to the target's orbit plane and when the ship crosses the two nodes.

local function target_list()
  local list = {}
  local active = props.get("/sim/active-ship")
  for i = 0, props.get("/ships/count") - 1 do
    if i ~= active then list[#list + 1] = props.get("/ships/ship[" .. i .. "]/name") end
  end
  for i = 0, props.get("/universe/bodies/count") - 1 do
    list[#list + 1] = props.get("/universe/bodies/body[" .. i .. "]/name")
  end
  return list
end

local function cycle_target(dir)
  local list = target_list()
  local cur, idx = props.get("/sim/target"), 0
  for i, n in ipairs(list) do if n == cur then idx = i end end
  idx = (idx - 1 + dir) % #list + 1
  props.set("/sim/target", list[idx])
end

local function fmt_time(s)
  if s == nil or s < 0 then return "--" end
  return string.format("%d:%02d:%02d", math.floor(s / 3600), math.floor((s % 3600) / 60), math.floor(s % 60))
end

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Align Planes",
  buttons = { "TGT+", "TGT-", "", "", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "ALIGN PLANES", 1)
    if not props.get("/target/valid") then
      g:colour(255, 200, 80)
      g:text(200, 200, "No target", 1)
      g:text(200, 222, "Press TGT+ / TGT- to select", 1)
      return
    end
    g:colour(120, 255, 140)
    g:text(200, 36, "Target: " .. props.get("/sim/target"), 1)
    if not props.get("/target/in-plane-data") then
      g:colour(255, 200, 80)
      g:text(200, 200, "Target not in this orbit's frame", 1)
      return
    end
    local cx, cy, r = 200, 190, 100
    g:colour(60, 110, 255)
    g:circle(cx, cy, r)
    local function at(deg, rad)
      local a = math.rad(deg)
      return cx + math.cos(a) * rad, cy - math.sin(a) * rad
    end
    local x, y = at(props.get("/target/an-true-anomaly"), r)
    g:colour(120, 255, 140)
    g:disc(x, y, 5)
    g:text(x, y - 14, "AN", 1)
    x, y = at(props.get("/target/dn-true-anomaly"), r)
    g:colour(255, 160, 90)
    g:disc(x, y, 5)
    g:text(x, y + 14, "DN", 1)
    x, y = at(ship("orbit/true-anomaly"), r)
    g:colour(255, 255, 255)
    g:disc(x, y, 4)
    g:colour(120, 255, 140)
    g:text(200, 310, string.format("Rel. inclination  %.3f deg", props.get("/target/relative-inclination")), 1)
    g:text(200, 332, "Time to AN  " .. fmt_time(props.get("/target/an-time")), 1)
    g:text(200, 350, "Time to DN  " .. fmt_time(props.get("/target/dn-time")), 1)
  end,

  button = function(self, i)
    if i == 1 then cycle_target(1) elseif i == 2 then cycle_target(-1) end
  end,
}
