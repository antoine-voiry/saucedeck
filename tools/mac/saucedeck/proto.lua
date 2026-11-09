-- SAUCEDECK helper protocol: framing, CRC and message codecs.
-- Byte-for-byte match of lib/deck_core/src/frame.h and helper_proto.h.
-- Pure Lua 5.4, no Hammerspoon dependencies (host-tested in test/lua).
local M = {}

local VERSION = 1
local REPORT = 63
local PAYLOAD_MAX = REPORT - 3
local FRAME = 2 + REPORT + 2
local SYNC = "\xA5\x5A"

-- host -> deck
M.HELLO, M.VOLUME, M.MIC, M.TIME, M.CARD = 0x01, 0x02, 0x03, 0x04, 0x05
-- deck -> host
M.ACTION, M.MIC_DELTA, M.MIC_MUTE = 0x81, 0x82, 0x83

M.ICONS = { none = 0, music = 1, calendar = 2, cpu = 3, weather = 4, mail = 5, chat = 6, battery = 7 }

-- CRC16-CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final xor.
function M.crc16(s)
  local c = 0xFFFF
  for i = 1, #s do
    c = c ~ (s:byte(i) << 8)
    for _ = 1, 8 do
      if c & 0x8000 ~= 0 then c = ((c << 1) ~ 0x1021) & 0xFFFF else c = (c << 1) & 0xFFFF end
    end
  end
  return c
end

function M.frame(mtype, payload)
  if #payload > PAYLOAD_MAX then return nil end
  local msg = string.char(VERSION, mtype, #payload) .. payload
  msg = msg .. string.rep("\0", REPORT - #msg)
  return SYNC .. msg .. string.pack("<I2", M.crc16(msg))
end

local function pct(v)
  v = math.floor(v + 0.5)
  return math.max(0, math.min(100, v))
end

function M.hello(ver) return M.frame(M.HELLO, string.char(ver)) end
function M.volume(p, muted) return M.frame(M.VOLUME, string.char(pct(p), muted and 1 or 0)) end
function M.mic(p, muted) return M.frame(M.MIC, string.char(pct(p), muted and 1 or 0)) end

function M.time(epoch, tzMin)
  if tzMin < -720 or tzMin > 840 then return nil end
  return M.frame(M.TIME, string.pack("<I4i2", epoch, tzMin))
end

function M.card(slot, icon, text, ttl)
  ttl = ttl or 0
  if slot < 0 or slot > 7 or icon < 0 or icon > 7 then return nil end
  if #text > 48 or text:find("[^\x20-\x7E]") then return nil end
  if ttl < 0 or ttl > 0xFFFF then return nil end
  return M.frame(M.CARD, string.char(slot, icon) .. string.pack("<I2", ttl) .. string.char(#text) .. text)
end

-- Stream reader: re-syncs on junk; on bad CRC drops one byte and rescans (like FrameReader).
local Reader = {}
Reader.__index = Reader

function M.reader() return setmetatable({ buf = "" }, Reader) end

function Reader:push(bytes)
  local out = {}
  local buf = self.buf .. bytes
  while true do
    local s = buf:find(SYNC, 1, true)
    if not s then
      -- keep a trailing lone A5 (possible sync start)
      buf = (buf:byte(-1) == 0xA5) and "\xA5" or ""
      break
    end
    if s > 1 then buf = buf:sub(s) end
    if #buf < FRAME then break end
    local msg = buf:sub(3, 2 + REPORT)
    if string.unpack("<I2", buf, FRAME - 1) == M.crc16(msg) then
      out[#out + 1] = msg
      buf = buf:sub(FRAME + 1)
    else
      buf = buf:sub(2)
    end
  end
  self.buf = buf
  return out
end

function M.decodeDeck(msg)
  if #msg ~= REPORT or msg:byte(1) ~= VERSION then return nil end
  local mtype, len = msg:byte(2), msg:byte(3)
  if len > PAYLOAD_MAX then return nil end
  local p = msg:sub(4, 3 + len)
  if mtype == M.MIC_DELTA then
    if len ~= 1 then return nil end
    local s = string.unpack("<i1", p)
    if s == 0 or s < -8 or s > 8 then return nil end
    return { type = "mic_delta", steps = s }
  elseif mtype == M.MIC_MUTE then
    if len ~= 0 then return nil end
    return { type = "mic_mute" }
  elseif mtype == M.ACTION then
    if len < 3 then return nil end
    local key, kind, vlen = p:byte(1, 3)
    if 3 + vlen ~= len then return nil end
    return { type = "action", key = key, kind = kind, value = p:sub(4) }
  end
  return nil
end

return M
