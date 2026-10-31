-- Smoke tests for tools/mac/saucedeck_helper.lua against a fake `hs` (run from the repo root).
local FakeHs = require("fake_hs")
local P = require("saucedeck.proto")
local t = {}

local function setup(ports)
  local hs = FakeHs{ ports = ports }
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

return t
