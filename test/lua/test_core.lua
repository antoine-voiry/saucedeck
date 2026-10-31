local Core = require("saucedeck.core")
local P = require("saucedeck.proto")
local t = {}

local function new() return Core.new{ clock = function() return 1793455200, 60 end, version = 1 } end
local function types(frames)
  local s = {}
  for _, f in ipairs(frames) do s[#s + 1] = string.format("%02x", f:byte(4)) end
  return table.concat(s, ",")
end
local function msg(f) return f:sub(3, 65) end

t.pick_port = function(T)
  local c = new()
  T.eq(c:pickPort({ "Bluetooth-Incoming-Port", "wchusbserial1410", "usbmodem2A0C2", "usbmodem1A0C1" }), "usbmodem1A0C1")
  T.eq(c:pickPort({ "usbserial-0001" }), nil); T.eq(c:pickPort({}), nil)
end
t.connect_sends_hello_time_then_known_state = function(T)
  local c = new()
  T.eq(types(c:connect(0)), "01,04")
  c:disconnect()
  c:setVolume(40, false); c:setMic(70, true); c:setCard(4, P.ICONS.weather, "Sunny"); c:setCard(1, 1, "Song")
  T.eq(types(c:connect(10)), "01,04,02,03,05,05")
  local fr = c:connect(20)
  T.eq(fr[5]:byte(6), 1)  -- cards ordered by slot: slot 1 first
end
t.time_frame_uses_clock = function(T)
  local f = new():connect(0)[2]
  T.eq(f, P.time(1793455200, 60))
end
t.tick_schedule = function(T)
  local c = new()
  T.eq(#c:tick(0), 0)          -- disconnected
  c:connect(0)
  T.eq(types(c:tick(0.5)), "")
  T.eq(types(c:tick(1.0)), "01")
  T.eq(types(c:tick(1.5)), "")
  T.eq(types(c:tick(2.1)), "01")
  T.eq(types(c:tick(60.0)), "01,04")
  T.eq(types(c:tick(60.2)), "")
end
t.set_volume_dedupes_and_needs_connection = function(T)
  local c = new()
  T.eq(#c:setVolume(50, false), 0)  -- remembered, not sent
  c:connect(0)
  T.eq(#c:setVolume(50.2, false), 0)
  local fr = c:setVolume(51, false)
  T.eq(#fr, 1); T.eq(fr[1], P.volume(51, false))
  T.eq(#c:setVolume(51, true), 1)
end
t.set_card_cleans_and_dedupes = function(T)
  local c = new(); c:connect(0)
  local fr = c:setCard(4, 4, "Patchy rain +13\xC2\xB0C")
  T.eq(fr[1], P.card(4, 4, "Patchy rain +13C"))
  T.eq(#c:setCard(4, 4, "Patchy rain +13C"), 0)
  T.eq(c:setCard(4, 4, nil)[1], P.card(4, 4, ""))
  T.eq(#c:setCard(4, 4, ""), 0)
end
t.cleared_card_not_resent_on_connect = function(T)
  local c = new(); c:setCard(2, 3, "x"); c:setCard(2, 3, nil)
  T.eq(types(c:connect(0)), "01,04")
end
t.mic_delta_command = function(T)
  local c = new(); c:setMic(50, false)
  local cmd = c:onDeck(msg(P.frame(P.MIC_DELTA, "\x02")))
  T.eq(cmd[1].cmd, "mic_volume"); T.eq(cmd[1].pct, 60)
  T.eq(c:onDeck(msg(P.frame(P.MIC_DELTA, "\xF8")))[1].pct, 10)
  c:setMic(98, false)
  T.eq(c:onDeck(msg(P.frame(P.MIC_DELTA, "\x03")))[1].pct, 100)
  c:setMic(2, false)
  T.eq(c:onDeck(msg(P.frame(P.MIC_DELTA, "\xFF")))[1].pct, 0)
end
t.mic_mute_toggles = function(T)
  local c = new()
  T.eq(c:onDeck(msg(P.frame(P.MIC_MUTE, "")))[1].muted, true)  -- unknown -> unmuted -> mute
  c:setMic(30, true)
  T.eq(c:onDeck(msg(P.frame(P.MIC_MUTE, "")))[1].muted, false)
end
t.action_ignored = function(T)
  T.eq(#new():onDeck(msg(P.frame(P.ACTION, "\x00\x01\x01x"))), 0)
  T.eq(#new():onDeck(string.rep("\0", 63)), 0)
end
t.release_pauses_60s = function(T)
  local c = new(); c:connect(0)
  c:release(5)
  T.eq(c:connected(), false); T.ok(c:paused(5)); T.ok(c:paused(64.9)); T.eq(c:paused(65), false)
  T.eq(#c:tick(6), 0)
end
return t
