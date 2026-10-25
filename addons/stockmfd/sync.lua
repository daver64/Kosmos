-- Sync Orbit MFD: range, closing speed and phase angle to the target, with the period ratio you need to close the gap.

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

local function fmt_len(m)
  local a = math.abs(m)
  if a >= 1e9 then return string.format("%.2f Gm", m / 1e9) end
  if a >= 1e6 then return string.format("%.1f Mm", m / 1e6) end
  if a >= 1e3 then return string.format("%.1f km", m / 1e3) end
  return string.format("%.0f m", m)
end

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

return {
  name = "Sync Orbit",
  buttons = { "TGT+", "TGT-", "", "", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    g:colour(255, 240, 140)
    g:text(200, 14, "SYNC ORBIT", 1)
    if not props.get("/target/valid") then
      g:colour(255, 200, 80)
      g:text(200, 200, "No target", 1)
      g:text(200, 222, "Press TGT+ / TGT- to select", 1)
      return
    end
    g:colour(120, 255, 140)
    g:text(200, 36, "Target: " .. props.get("/sim/target"), 1)
    g:text(30, 64, "Range      " .. fmt_len(props.get("/target/distance")), 0)
    g:text(30, 82, string.format("Rel. speed  %.1f m/s", props.get("/target/relative-speed")), 0)
    g:text(30, 100, string.format("Closing     %.1f m/s", props.get("/target/closing-speed")), 0)
    if not props.get("/target/in-plane-data") then
      g:colour(255, 200, 80)
      g:text(200, 220, "Target not in this orbit's frame", 1)
      return
    end
    local phase = props.get("/target/phase-angle")
    local cx, cy, r1, r2 = 200, 240, 55, 95
    g:colour(60, 110, 255)
    g:circle(cx, cy, r1)
    g:circle(cx, cy, r2)
    g:colour(255, 255, 255)
    g:disc(cx + r1, cy, 4)
    local a = math.rad(phase)
    g:colour(255, 200, 80)
    g:disc(cx + math.cos(a) * r2, cy - math.sin(a) * r2, 4)
    g:colour(120, 255, 140)
    g:line(cx, cy, cx + r1, cy)
    g:line(cx, cy, cx + math.cos(a) * r2, cy - math.sin(a) * r2)
    g:text(30, 355, string.format("Phase  %.1f deg", phase), 0)
    local pt = props.get("/target/orbit/period")
    local ps = ship("orbit/period")
    if pt and pt > 0 and ps > 0 then g:text(370, 355, string.format("Tship/Ttgt %.3f", ps / pt), 2) end
    g:text(30, 372, string.format("Rel. inc  %.2f deg", props.get("/target/relative-inclination")), 0)
  end,

  button = function(self, i)
    if i == 1 then cycle_target(1) elseif i == 2 then cycle_target(-1) end
  end,
}
