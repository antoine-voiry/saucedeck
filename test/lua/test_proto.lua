local P = require("saucedeck.proto")
local t = {}

t.crc_check_value = function(T) T.eq(P.crc16("123456789"), 0x29B1) end

t.hello_matches_helper_sim = function(T)
  T.eq(T.hex(P.hello(1)), "a55a01010101" .. string.rep("00", 59) .. "ba19")
end
t.volume_matches_helper_sim = function(T)
  T.eq(T.hex(P.volume(42, true)), "a55a0102022a01" .. string.rep("00", 58) .. "4b86")
end
t.time_matches_helper_sim = function(T)
  T.eq(T.hex(P.time(1793455200, 60)), "a55a01040660f4e56a3c00" .. string.rep("00", 54) .. "b6d6")
end
t.card_matches_helper_sim = function(T)
  T.eq(T.hex(P.card(3, P.ICONS.cpu, "CPU 12% RAM 61%")),
    "a55a010514030300000f435055203132252052414d20363125" .. string.rep("00", 40) .. "63c2")
end
t.frame_size_and_padding = function(T)
  T.eq(#P.hello(1), 67)
  T.eq(#P.card(0, 0, string.rep("x", 48)), 67)
end
t.volume_clamps_and_rounds = function(T)
  T.eq(P.volume(140, false):byte(6), 100)
  T.eq(P.volume(-3, false):byte(6), 0)
  T.eq(P.volume(41.6, false):byte(6), 42)
end
t.time_rejects_bad_tz = function(T)
  T.eq(P.time(0, 841), nil); T.eq(P.time(0, -721), nil); T.ok(P.time(0, -720)); T.ok(P.time(0, 840))
end
t.time_negative_tz_is_twos_complement = function(T)
  local f = P.time(0, -300)            -- 0xFED4
  T.eq(f:byte(10), 0xD4); T.eq(f:byte(11), 0xFE)
end
t.card_rejects_bad_input = function(T)
  T.eq(P.card(8, 0, "x"), nil); T.eq(P.card(0, 8, "x"), nil)
  T.eq(P.card(0, 0, string.rep("x", 49)), nil); T.eq(P.card(0, 0, "caf\xC3\xA9"), nil)
  T.eq(P.card(0, 0, "a\nb"), nil)
end
t.card_empty_text_clears = function(T)
  local f = P.card(2, 3, "")
  T.ok(f); T.eq(f:byte(5), 5); T.eq(f:byte(10), 0)  -- len 5, tlen 0
end
t.card_ttl_le = function(T)
  local f = P.card(1, 1, "x", 300)     -- 0x012C
  T.eq(f:byte(8), 0x2C); T.eq(f:byte(9), 0x01)
end

local function deckMsg(mtype, payload)
  return P.frame(mtype, payload):sub(3, 65)
end
t.reader_resyncs_through_junk = function(T)
  local r = P.reader()
  local f = P.frame(P.MIC_MUTE, "")
  local out = r:push("\x00\xA5\xA5\x5Ajunk" .. f:sub(1, 30))
  T.eq(#out, 0)
  out = r:push(f:sub(31) .. f)
  T.eq(#out, 2); T.eq(#out[1], 63); T.eq(out[1]:byte(2), P.MIC_MUTE)
end
t.reader_drops_bad_crc = function(T)
  local r = P.reader()
  local f = P.frame(P.MIC_MUTE, "")
  local bad = f:sub(1, 66) .. string.char((f:byte(67) + 1) % 256)
  T.eq(#r:push(bad), 0)
  T.eq(#r:push(f), 1)
end
t.decode_mic_delta = function(T)
  local m = P.decodeDeck(deckMsg(P.MIC_DELTA, "\xFD"))
  T.eq(m.type, "mic_delta"); T.eq(m.steps, -3)
  T.eq(P.decodeDeck(deckMsg(P.MIC_DELTA, "\x09")), nil)
  T.eq(P.decodeDeck(deckMsg(P.MIC_DELTA, "\x00")), nil)
end
t.decode_mic_mute = function(T) T.eq(P.decodeDeck(deckMsg(P.MIC_MUTE, "")).type, "mic_mute") end
t.decode_action = function(T)
  local m = P.decodeDeck(deckMsg(P.ACTION, "\x02\x01\x05Safar"))
  T.eq(m.type, "action"); T.eq(m.key, 2); T.eq(m.kind, 1); T.eq(m.value, "Safar")
end
t.decode_rejects_bad_version_and_unknown = function(T)
  local m = deckMsg(P.MIC_MUTE, "")
  T.eq(P.decodeDeck("\x02" .. m:sub(2)), nil)
  T.eq(P.decodeDeck(deckMsg(0x42, "")), nil)
  T.eq(P.decodeDeck("short"), nil)
end
return t
