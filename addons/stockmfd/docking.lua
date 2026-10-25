-- Docking MFD: where the target docking port (tuned on a NAV receiver as an IDS beacon) sits relative to the
-- selected port on this ship. Capture is automatic when close, slow and roughly lined up.

local function n(path) return props.get(path) or 0 end

local function ids_items()
  local list = {}
  for k = 0, n("/nav/count") - 1 do
    if props.get("/nav/item[" .. k .. "]/type") == "ids" then
      list[#list + 1] = props.get("/nav/item[" .. k .. "]/frequency")
    end
  end
  return list
end

local function tune_next()
  local list = ids_items()
  if #list == 0 then return end
  local rec = n("/dock/receiver")
  local path = "/nav/nav[" .. rec .. "]/frequency"
  local cur, pick = n(path), list[1]
  for i, f in ipairs(list) do
    if math.abs(f - cur) < 0.026 then pick = list[i % #list + 1] end
  end
  props.set(path, pick)
end

local function ok_colour(g, good)
  if good then g:colour(120, 255, 140) else g:colour(255, 200, 80) end
end

return {
  name = "Docking",
  buttons = { "RCV", "PRT", "TUNE", "UNDK", "", "", "", "", "", "", "", "" },
  init = function(self) end,

  draw = function(self, g, w, h)
    local rec = n("/dock/receiver")
    g:colour(255, 240, 140)
    g:text(200, 14, "DOCKING", 1)
    g:colour(120, 200, 255)
    g:text(8, 14, string.format("NAV%d %.2f", rec + 1, n("/nav/nav[" .. rec .. "]/frequency")), 0)
    if n("/dock/port-count") == 0 then
      g:colour(255, 120, 100)
      g:text(200, 200, "This ship has no docking port", 1)
      return
    end
    g:colour(200, 210, 220)
    local busy = props.get("/dock/own-port-busy")
    g:text(392, 14, (props.get("/dock/own-port-name") or "") .. (busy and " (in use)" or ""), 2)

    if props.get("/dock/docked") then
      g:colour(120, 255, 140)
      g:text(200, 190, "DOCKED", 1)
      g:text(200, 214, props.get("/dock/partner") or "", 1)
      g:colour(200, 210, 220)
      g:text(200, 250, "UNDK to release", 1)
      return
    end
    if not props.get("/dock/valid") then
      g:colour(255, 200, 80)
      g:text(200, 190, "No IDS signal", 1)
      g:text(200, 214, "TUNE selects the nearest docking beacon", 1)
      return
    end

    local x, y, z, range = n("/dock/x"), n("/dock/y"), n("/dock/z"), n("/dock/range")
    local yaw, pitch, roll = n("/dock/yaw"), n("/dock/pitch"), n("/dock/roll")
    local cx, cy, half = 200, 170, 100
    g:colour(60, 110, 255)
    g:rect(cx - half, cy - half, 2 * half, 2 * half)
    g:line(cx - half, cy, cx + half, cy)
    g:line(cx, cy - half, cx, cy + half)
    g:circle(cx, cy, half / 2)

    -- The view spans a fifth of the range either side, never less than a metre.
    local span = math.max(1.0, range * 0.2)
    local mx = math.max(-half, math.min(half, x / span * half))
    local my = math.max(-half, math.min(half, -y / span * half))
    local aligned = math.abs(yaw) < 5 and math.abs(pitch) < 5
    ok_colour(g, math.abs(x) < 0.5 and math.abs(y) < 0.5)
    g:circle(cx + mx, cy + my, 8)
    g:line(cx + mx - 12, cy + my, cx + mx + 12, cy + my)
    g:line(cx + mx, cy + my - 12, cx + mx, cy + my + 12)
    -- Misalignment of the target port's axis: a tick from the marker.
    ok_colour(g, aligned)
    g:line(cx + mx, cy + my, cx + mx + math.max(-40, math.min(40, yaw * 2)), cy + my - math.max(-40, math.min(40, pitch * 2)))

    g:colour(120, 255, 140)
    g:text(8, 290, string.format("Range    %8.2f m", range), 0)
    g:text(8, 306, string.format("Closing  %8.2f m/s", n("/dock/closing-speed")), 0)
    g:text(8, 322, string.format("Lateral  %5.2f %5.2f m/s", n("/dock/vx"), n("/dock/vy")), 0)
    g:text(8, 338, string.format("Offset   %5.2f %5.2f m", x, y), 0)
    ok_colour(g, aligned and math.abs(roll) < 10)
    g:text(220, 306, string.format("Yaw   %6.1f", yaw), 0)
    g:text(220, 322, string.format("Pitch %6.1f", pitch), 0)
    g:text(220, 338, string.format("Roll  %6.1f", roll), 0)
    g:colour(200, 210, 220)
    g:text(200, 372, props.get("/dock/target") or "", 1)
    if z < 0 then
      g:colour(255, 120, 100)
      g:text(200, 390, "target behind the port", 1)
    end
  end,

  button = function(self, i)
    if i == 1 then
      props.set("/dock/receiver", 1 - n("/dock/receiver"))
    elseif i == 2 then
      local count = n("/dock/port-count")
      if count > 0 then props.set("/dock/own-port", (n("/dock/own-port") + 1) % count) end
    elseif i == 3 then
      tune_next()
    elseif i == 4 then
      props.set("/dock/undock", true)
    end
  end,
}
