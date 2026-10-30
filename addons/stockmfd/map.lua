-- Map MFD: the surface of the body you are over, the predicted ground track of the orbit, the impact point of a
-- descent and an optional target base.

local function ship(path)
  return props.get("/ships/ship[" .. props.get("/sim/active-ship") .. "]/" .. path)
end

-- Surface images shipped in data/textures/.
local TEXTURES = {
  earth = true, moon = true, mars = true, mercury = true, venus = true, io = true, europa = true,
  ganymede = true, callisto = true, titan = true, pluto = true, charon = true, triton = true,
  rhea = true, tethys = true, dione = true, iapetus = true, phobos = true, deimos = true,
}

local function fmt_time(s)
  if s == nil or s ~= s or s < 0 then return "--" end
  s = math.floor(s)
  if s >= 86400 then return string.format("%dd %02dh", s // 86400, (s % 86400) // 3600) end
  if s >= 3600 then return string.format("%dh %02dm", s // 3600, (s % 3600) // 60) end
  return string.format("%dm %02ds", s // 60, s % 60)
end

-- Great-circle distance (m) and initial bearing (degrees) between two points on a sphere of radius r.
local function course(lat1, lon1, lat2, lon2, r)
  local p1, p2, dl = math.rad(lat1), math.rad(lat2), math.rad(lon2 - lon1)
  local a = math.sin((p2 - p1) / 2) ^ 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ^ 2
  local d = 2 * r * math.asin(math.min(1, math.sqrt(a)))
  local y = math.sin(dl) * math.cos(p2)
  local x = math.cos(p1) * math.sin(p2) - math.sin(p1) * math.cos(p2) * math.cos(dl)
  return d, (math.deg(math.atan(y, x)) + 360) % 360
end

-- Bases on the body being flown over.
local function bases_here()
  local list, body = {}, ship("soi")
  for i = 0, (props.get("/sim/bases/count") or 0) - 1 do
    local at = "/sim/bases/base[" .. i .. "]/"
    if props.get(at .. "body") == body then
      list[#list + 1] = { id = props.get(at .. "id"), name = props.get(at .. "name"),
        lat = props.get(at .. "latitude"), lon = props.get(at .. "longitude") }
    end
  end
  return list
end

return {
  name = "Map",
  buttons = { "BASE+", "BASE-", "NONE", "", "", "", "TEXTURE", "GRID", "", "", "", "" },

  init = function(self)
    self.base = nil
    self.texture = true
    self.grid = true
  end,

  draw = function(self, g, w, h)
    local x0, y0, mw, mh = 20, 34, 360, 180
    local function mx(lon) return x0 + (lon + 180) / 360 * mw end
    local function my(lat) return y0 + (90 - lat) / 180 * mh end
    local body = ship("soi")
    local radius = ship("body-radius")

    g:colour(255, 240, 140)
    g:text(200, 14, "MAP  " .. body, 1)

    if TEXTURES[string.lower(body)] and self.texture then
      g:image("../../data/textures/" .. string.lower(body) .. ".jpg", x0 + mw / 2, y0 + mh / 2, mw, mh)
    end
    if self.grid then
      g:colour(60, 120, 80)
      for lon = -150, 150, 30 do g:line(mx(lon), y0, mx(lon), y0 + mh) end
      for lat = -60, 60, 30 do g:line(x0, my(lat), x0 + mw, my(lat)) end
      g:colour(100, 200, 130)
      g:line(x0, my(0), x0 + mw, my(0))
    end
    g:colour(100, 200, 130)
    g:rect(x0, y0, x0 + mw, y0 + mh)

    local bases = bases_here()
    local target
    for _, b in ipairs(bases) do
      if b.id == self.base then target = b end
      g:colour(120, 200, 255)
      g:rect(mx(b.lon) - 2, my(b.lat) - 2, mx(b.lon) + 2, my(b.lat) + 2)
    end

    -- Predicted ground track: one orbit from the ship's position, or the path down to the ground.
    local n = ship("map/points")
    g:colour(255, 200, 80)
    local pl, pm
    for i = 0, n - 1 do
      local la, lo = ship("map/point[" .. i .. "]/lat"), ship("map/point[" .. i .. "]/lon")
      if pl and math.abs(lo - pm) < 180 then g:line(mx(pm), my(pl), mx(lo), my(la)) end
      pl, pm = la, lo
    end

    local impact = ship("map/impact") > 0.5
    local ilat, ilon = ship("map/impact-latitude"), ship("map/impact-longitude")
    if impact then
      g:colour(255, 80, 60)
      local ix, iy = mx(ilon), my(ilat)
      g:line(ix - 5, iy - 5, ix + 5, iy + 5)
      g:line(ix - 5, iy + 5, ix + 5, iy - 5)
    end

    local lon, lat = ship("longitude"), ship("latitude")
    local sx, sy = mx(lon), my(lat)
    if target then
      g:colour(120, 200, 255)
      g:circle(mx(target.lon), my(target.lat), 6)
      if math.abs(target.lon - lon) < 180 then g:line(sx, sy, mx(target.lon), my(target.lat)) end
    end
    g:colour(255, 255, 255)
    g:circle(sx, sy, 5)
    g:line(sx - 9, sy, sx + 9, sy)
    g:line(sx, sy - 9, sx, sy + 9)

    local ty = 232
    g:colour(120, 255, 140)
    g:text(20, ty, string.format("LAT %7.3f", lat), 0)
    g:text(20, ty + 16, string.format("LON %8.3f", lon), 0)
    g:text(20, ty + 32, string.format("ALT %.1f km", ship("altitude") / 1000), 0)
    g:text(20, ty + 48, string.format("INC %.2f", ship("orbit/inclination")), 0)

    local bound = ship("orbit/period") > 0
    g:text(150, ty, "PER " .. (bound and fmt_time(ship("orbit/period")) or "--"), 0)
    g:text(150, ty + 16, "T-PE " .. (bound and fmt_time(ship("orbit/time-to-periapsis")) or "--"), 0)
    g:text(150, ty + 32, "T-AP " .. (bound and fmt_time(ship("orbit/time-to-apoapsis")) or "--"), 0)
    if impact then
      g:colour(255, 110, 90)
      g:text(150, ty + 48, "IMPACT " .. fmt_time(ship("map/impact-time")), 0)
      local d = course(lat, lon, ilat, ilon, radius)
      g:text(150, ty + 64, string.format("%.1f %.1f  %.0f km", ilat, ilon, d / 1000), 0)
    else
      g:text(150, ty + 48, "NO IMPACT", 0)
    end

    g:colour(120, 200, 255)
    if target then
      local d, brg = course(lat, lon, target.lat, target.lon, radius)
      g:text(290, ty, string.sub(target.name, 1, 22), 0)
      g:text(290, ty + 16, string.format("DST %.1f km", d / 1000), 0)
      g:text(290, ty + 32, string.format("BRG %03d", math.floor(brg + 0.5) % 360), 0)
    else
      g:text(290, ty, #bases > 0 and "BASE: none" or "no bases here", 0)
    end
  end,

  button = function(self, i)
    local list = bases_here()
    local idx = 0
    for k, b in ipairs(list) do if b.id == self.base then idx = k end end
    if i == 1 and #list > 0 then self.base = list[idx % #list + 1].id
    elseif i == 2 and #list > 0 then self.base = list[(idx - 2) % #list + 1].id
    elseif i == 3 then self.base = nil
    elseif i == 7 then self.texture = not self.texture
    elseif i == 8 then self.grid = not self.grid end
  end,
}
