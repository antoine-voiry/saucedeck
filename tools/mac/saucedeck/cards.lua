-- SAUCEDECK status card formatters: raw Mac data in, short ASCII text out.
-- Pure Lua 5.4, no Hammerspoon dependencies (host-tested in test/lua).
local text = require("saucedeck.text")

local M = {}

local function nonEmpty(s)
  s = text.clean(s)
  return s ~= "" and s or nil
end

local function round(x) return math.floor(x + 0.5) end

local function firstLine(s)
  if type(s) ~= "string" then return nil end
  for line in s:gmatch("[^\r\n]+") do
    local c = nonEmpty(line)
    if c then return c end
  end
  return nil
end

function M.meeting(icalOut)
  if type(icalOut) ~= "string" then return nil end
  for line in icalOut:gmatch("[^\r\n]+") do
    line = line:gsub("^%s+", "")
    line = line:gsub("^\xE2\x80\xA2", ""):gsub("^[*-]", "")
    local c = nonEmpty(line)
    if c then return c end
  end
  return nil
end

function M.nowPlaying(artist, title, playing)
  if not playing then return nil end
  local ti = nonEmpty(title)
  if not ti then return nil end
  local ar = nonEmpty(artist)
  return text.clean(ar and (ar .. " - " .. ti) or ti)
end

function M.stats(cpuPct, ramPct)
  return text.clean(string.format("CPU %d%% RAM %d%%", round(cpuPct), round(ramPct)))
end

function M.ramPercent(vm)
  if type(vm) ~= "table" then return nil end
  local a, w, c = vm.pagesActive, vm.pagesWiredDown, vm.pagesUsedByVMCompressor
  local ps, mem = vm.pageSize, vm.memSize
  if not (a and w and c and ps and mem) or mem == 0 then return nil end
  return (a + w + c) * ps / mem * 100
end

function M.battery(pct, charging)
  if not pct then return nil end
  return text.clean(string.format("Battery %d%%%s", round(pct), charging and " charging" or ""))
end

function M.weather(body)
  if type(body) ~= "string" or body:find("<", 1, true) then return nil end
  local line = firstLine(body)
  if not line or line:sub(1, 7) == "Unknown" then return nil end
  return line
end

function M.badge(lsappinfoOut)
  if type(lsappinfoOut) ~= "string" then return 0 end
  local label = lsappinfoOut:match('"label"="([^"]*)"')
  if not label or label == "" then return 0 end
  return math.tointeger(tonumber(label)) or 1
end

function M.unread(list)
  local parts = {}
  for _, e in ipairs(list or {}) do
    if e[2] and e[2] > 0 then parts[#parts + 1] = e[1] .. " " .. e[2] end
  end
  if #parts == 0 then return nil end
  return text.clean(table.concat(parts, " "))
end

return M
