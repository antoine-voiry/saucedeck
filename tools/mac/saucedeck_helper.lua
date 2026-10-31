-- SAUCEDECK status helper: thin Hammerspoon glue over tools/mac/saucedeck/*.lua.
-- Load from ~/.hammerspoon/init.lua:
--   dofile(os.getenv("HOME") .. "/saucedeck/tools/mac/saucedeck_helper.lua")
-- All logic (framing, dedupe, keep-alive, pause) lives in the pure modules; this file only wires
-- Hammerspoon APIs to them. Not bench-verified: needs a deck on USB.
local dir = (debug.getinfo(1, "S").source:sub(2):match("^(.*)/[^/]*$")) or "."
package.path = dir .. "/?.lua;" .. package.path

require("hs.ipc") -- makes the `hs` CLI work (pre-upload release hook)

if rawget(_G, "saucedeck") and type(saucedeck.stop) == "function" then pcall(saucedeck.stop) end

local Core = require("saucedeck.core")
local cards = require("saucedeck.cards")
local proto = require("saucedeck.proto")

local ICON = proto.ICONS
local BAUD = 115200

local function now() return hs.timer.absoluteTime() / 1e9 end

-- Local UTC offset in minutes (DST aware).
local function tzMinutes()
  local t = os.time()
  local u = os.date("!*t", t)
  u.isdst = os.date("*t", t).isdst
  return math.floor(os.difftime(t, os.time(u)) / 60 + 0.5)
end

local core = Core.new{ clock = function() return os.time(), tzMinutes() end, version = 1 }
local reader = proto.reader()
local port, portName, timers, running = nil, nil, {}, {} -- running: live hs.task objects (Hammerspoon may GC unreferenced ones)

local function closePort()
  if port then pcall(function() port:close() end) end
  port, portName = nil, nil
end

local function disconnect()
  core:disconnect()
  closePort()
end

local function send(frames)
  if not port then return end
  for _, f in ipairs(frames or {}) do
    local ok = pcall(function() port:sendData(f) end)
    if not ok then disconnect(); return end
  end
end

-- Run one card source; error or nil -> clear the slot.
local function setCard(slot, icon, fn)
  local ok, str = pcall(fn)
  if not ok then str = nil end
  send(core:setCard(slot, icon, str))
end

local function runCommand(c)
  local dev = hs.audiodevice.defaultInputDevice()
  if not dev then return end
  if c.cmd == "mic_volume" then
    dev:setInputVolume(c.pct)
  elseif c.cmd == "mic_mute" then
    dev:setInputMuted(c.muted)
  end
  send(core:setMic(dev:inputVolume() or c.pct or 0, dev:inputMuted()))
end

local function onSerial(_, msg, data)
  if msg == "received" then
    for _, m in ipairs(reader:push(data or "")) do
      for _, c in ipairs(core:onDeck(m)) do pcall(runCommand, c) end
    end
  elseif msg == "removed" or msg == "closed" or msg == "error" then
    disconnect()
  end
end

local function pollAudio()
  local out = hs.audiodevice.defaultOutputDevice()
  if out and out:outputVolume() then send(core:setVolume(out:outputVolume(), out:outputMuted())) end
  local inp = hs.audiodevice.defaultInputDevice()
  if inp and inp:inputVolume() then send(core:setMic(inp:inputVolume(), inp:inputMuted())) end
end

local function tryConnect()
  if not core:deckAttached(hs.usb.attachedDevices()) then return end
  local name = core:pickPort(hs.serial.availablePortNames())
  if not name then return end
  local p = hs.serial.newFromName(name)
  if not p then return end
  p:baudRate(BAUD):callback(onSerial)
  local ok = p:open()
  if not ok then pcall(p.close, p); return end
  port, portName = p, name
  reader = proto.reader()
  pollAudio() -- seed levels so connect() sends them and mic deltas start from the real value
  send(core:connect(now()))
end

local function everySecond()
  local t = now()
  if core:connected() then
    local gone = true
    for _, n in ipairs(hs.serial.availablePortNames()) do
      if n == portName then gone = false end
    end
    if not port or not port:isOpen() or gone then
      disconnect()
      return
    end
    send(core:tick(t))
  elseif not core:paused(t) then
    tryConnect()
  end
end

-- Async card helpers --------------------------------------------------------------------------

local function runTask(path, args, onDone)
  local ok = pcall(function()
    local task
    task = hs.task.new(path, function(code, out)
      running[task] = nil
      onDone(code == 0 and out or nil)
    end, args)
    running[task] = true
    task:start()
  end)
  if not ok then onDone(nil) end
end

local function refreshCalendar()
  local args = { "-n", "-nc", "-nrd", "-ea", "-li", "1", "-b", "", "-iep", "datetime,title",
                 "-po", "datetime,title", "-df", "", "-tf", "%H:%M", "eventsToday" }
  runTask("/opt/homebrew/bin/icalBuddy", args, function(out)
    setCard(0, ICON.calendar, function() return cards.meeting(out) end)
  end)
end

local function refreshMusic()
  setCard(1, ICON.music, function()
    local players = {
      { "Spotify", hs.spotify },
      { "Music", hs.itunes },
      { "iTunes", hs.itunes },
    }
    for _, pl in ipairs(players) do
      if hs.application.get(pl[1]) then
        local app = pl[2]
        if app.isPlaying() then
          return cards.nowPlaying(app.getCurrentArtist(), app.getCurrentTrack(), true)
        end
      end
    end
    return nil
  end)
end

local function refreshStats()
  setCard(2, ICON.cpu, function()
    return cards.stats(hs.host.cpuUsage().overall.active, cards.ramPercent(hs.host.vmStat()))
  end)
end

local function refreshBattery()
  setCard(3, ICON.battery, function() return cards.battery(hs.battery.percentage(), hs.battery.isCharging()) end)
end

local function refreshWeather()
  local ok = pcall(function()
    hs.http.asyncGet("https://wttr.in/?format=%C+%t", nil, function(status, body)
      setCard(4, ICON.weather, function() return status == 200 and cards.weather(body) or nil end)
    end)
  end)
  if not ok then setCard(4, ICON.weather, function() return nil end) end
end

local function refreshUnread()
  local apps, counts, pending = { "Mail", "Telegram" }, {}, 2
  for i, app in ipairs(apps) do
    runTask("/usr/bin/lsappinfo", { "info", "-only", "StatusLabel", app }, function(out)
      counts[i] = { app, cards.badge(out) }
      pending = pending - 1
      if pending == 0 then setCard(5, ICON.mail, function() return cards.unread(counts) end) end
    end)
  end
end

-- Public API ----------------------------------------------------------------------------------

local api = {}

function api.release(seconds)
  seconds = tonumber(seconds) or 60
  core:release(now(), seconds)
  closePort()
  hs.alert.show("SAUCEDECK helper paused " .. seconds .. " s")
end

function api.resume()
  core:resume()
  hs.alert.show("SAUCEDECK helper resumed")
end

function api.status()
  local t = now()
  return string.format("connected=%s paused=%s port=%s", tostring(core:connected()),
    tostring(core:paused(t)), port and "open" or "closed")
end

function api.stop()
  for _, tm in ipairs(timers) do pcall(function() tm:stop() end) end
  for tk in pairs(running) do pcall(function() tk:terminate() end) end
  timers, running = {}, {}
  closePort()
  core:disconnect()
end

saucedeck = api

local function every(sec, fn)
  timers[#timers + 1] = hs.timer.doEvery(sec, function() pcall(fn) end)
end

every(1, everySecond)
every(0.2, pollAudio)
every(60, refreshCalendar)
every(5, refreshMusic)
every(5, refreshStats)
every(60, refreshBattery)
every(900, refreshWeather)
every(30, refreshUnread)

-- Fill cards once now instead of waiting a full period.
for _, fn in ipairs({ refreshCalendar, refreshMusic, refreshStats, refreshBattery, refreshWeather, refreshUnread }) do
  pcall(fn)
end
