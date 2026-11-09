-- Smoke tests for tools/mac/saucedeck_helper.lua against a fake `hs` (run from the repo root).
local FakeHs = require("fake_hs")
local P = require("saucedeck.proto")
local t = {}

local function setup(ports, opts)
  opts = opts or {}
  opts.ports = ports
  local hs = FakeHs(opts)
  _G.hs = hs
  dofile("tools/mac/saucedeck_helper.lua")
  return hs
end
local function teardown()
  if _G.saucedeck then _G.saucedeck.stop() end
  _G.hs, _G.saucedeck = nil, nil
  package.preload["hs.ipc"] = nil
end
local function secondTimer(hs) -- the 1 s timer
  for _, tm in ipairs(hs.timers) do if tm.sec == 1 then return tm end end
end
local function deckFrame(mtype, payload) return P.frame(mtype, payload) end

t.loads_and_exposes_api = function(T)
  local hs = setup({})
  T.eq(type(saucedeck.release), "function")
  T.eq(type(saucedeck.status()), "string")
  teardown()
end

t.connects_on_timer_and_sends_hello = function(T)
  local hs = setup({ "usbmodemX" })
  secondTimer(hs).fn()
  T.eq(#hs.opened, 1); T.eq(hs.opened[1].name, "usbmodemX"); T.eq(hs.opened[1].baud, 115200)
  T.eq(hs.sent[1], P.hello(1))
  teardown()
end

t.mic_delta_from_deck_sets_input_volume = function(T)
  local hs = setup({ "usbmodemX" })
  secondTimer(hs).fn()
  hs.serialCallback(nil, "received", deckFrame(P.MIC_DELTA, "\x02"))
  T.eq(hs.inDev.vol, 60)
  T.eq(hs.sent[#hs.sent], P.mic(60, false))
  teardown()
end

t.release_closes_and_pauses = function(T)
  local hs = setup({ "usbmodemX" })
  secondTimer(hs).fn()
  saucedeck.release()
  T.ok(hs.closed >= 1, "port closed")
  T.eq(#hs.alerts, 1)
  secondTimer(hs).fn()
  T.eq(#hs.opened, 1)
  teardown()
end

t.removed_disconnects_and_reconnects = function(T)
  local hs = setup({ "usbmodemX" })
  secondTimer(hs).fn()
  hs.serialCallback(nil, "removed")
  secondTimer(hs).fn()
  T.eq(#hs.opened, 2)
  teardown()
end

t.running_task_survives_gc_and_callback_fires = function(T)
  local hs = setup({ "usbmodemX" })
  T.ok(hs.taskCount > 0, "tasks started")
  collectgarbage(); collectgarbage()
  local alive = 0
  for _ in pairs(hs.tasks) do alive = alive + 1 end
  T.eq(alive, hs.taskCount)
  local cal = hs.tasks[1]
  cal.cb(0, "")
  cal = nil
  collectgarbage(); collectgarbage()
  alive = 0
  for _ in pairs(hs.tasks) do alive = alive + 1 end
  T.eq(alive, hs.taskCount - 1) -- finished task released
  teardown()
end

t.bringup_product_not_opened = function(T)
  local hs = setup({ "usbmodemX" }, { devices = { { productName = "SAUCEDECK-BRINGUP" } } })
  secondTimer(hs).fn()
  T.eq(#hs.opened, 0)
  teardown()
end

t.release_seconds_and_resume = function(T)
  local hs = setup({ "usbmodemX" })
  saucedeck.release(3600)
  hs.nowNs = 1e9 + 1000e9
  secondTimer(hs).fn()
  T.eq(#hs.opened, 0, "still paused after 1000 s")
  saucedeck.resume()
  secondTimer(hs).fn()
  T.eq(#hs.opened, 1)
  teardown()
end

t.closed_and_error_disconnect = function(T)
  for _, msg in ipairs({ "closed", "error" }) do
    local hs = setup({ "usbmodemX" })
    secondTimer(hs).fn()
    hs.serialCallback(nil, msg)
    T.eq(saucedeck.status():match("connected=(%a+)"), "false", msg)
    teardown()
  end
end

t.timer_detects_closed_port = function(T)
  local hs = setup({ "usbmodemX" })
  secondTimer(hs).fn()
  hs.portIsOpen = false
  secondTimer(hs).fn()
  T.eq(saucedeck.status():match("connected=(%a+)"), "false")
  teardown()
end

t.timer_detects_vanished_port_name = function(T)
  local hs = setup({ "usbmodemX" })
  secondTimer(hs).fn()
  hs.ports = {}
  hs.serial.availablePortNames = function() return {} end
  secondTimer(hs).fn()
  T.eq(saucedeck.status():match("connected=(%a+)"), "false")
  teardown()
end

t.music_not_queried_without_apps = function(T)
  local hs = setup({}, {})
  T.eq(hs.musicCalls, 0)
  teardown()
end

t.music_queried_when_app_running = function(T)
  local hs = setup({}, { apps = { Spotify = true } })
  T.ok(hs.musicCalls > 0, "spotify queried")
  teardown()
end

t.failed_open_closes_port = function(T)
  local hs = setup({ "usbmodemX" }, { openFails = true })
  secondTimer(hs).fn()
  T.eq(#hs.opened, 1)
  T.eq(hs.closed, 1)
  teardown()
end

return t
