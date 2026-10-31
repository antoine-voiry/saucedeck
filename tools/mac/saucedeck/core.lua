-- SAUCEDECK helper link state machine: keep-alive schedule, dedupe, mic commands, release pause.
-- Pure Lua 5.4, no Hammerspoon dependencies (host-tested in test/lua).
local proto = require("saucedeck.proto")
local text = require("saucedeck.text")

local M = {}
M.__index = M

local HELLO_PERIOD = 1
local TIME_PERIOD = 60
local RELEASE_PAUSE = 60
local MIC_STEP = 5

function M.new(opts)
  return setmetatable({
    clock = opts.clock,
    version = opts.version or 1,
    isConnected = false,
    nextHello = 0,
    nextTime = 0,
    pausedUntil = 0,
    volume = nil,  -- { pct, muted }
    mic = nil,
    cards = {},    -- slot -> { icon, text }
  }, M)
end

local function round(v) return math.max(0, math.min(100, math.floor(v + 0.5))) end

-- Append f to list only when the codec produced a frame.
local function add(list, f)
  if f then list[#list + 1] = f end
  return list
end

function M:pickPort(names)
  local best
  for _, n in ipairs(names) do
    if n:sub(1, 8) == "usbmodem" and (not best or n < best) then best = n end
  end
  return best
end

function M:timeFrame()
  local epoch, tz = self.clock()
  return proto.time(epoch, tz)
end

function M:connect(now)
  self.isConnected = true
  self.nextHello = now + HELLO_PERIOD
  self.nextTime = now + TIME_PERIOD
  local out = {}
  add(out, proto.hello(self.version))
  add(out, self:timeFrame())
  if self.volume then add(out, proto.volume(self.volume.pct, self.volume.muted)) end
  if self.mic then add(out, proto.mic(self.mic.pct, self.mic.muted)) end
  local slots = {}
  for slot, c in pairs(self.cards) do
    if c.text ~= "" then slots[#slots + 1] = slot end
  end
  table.sort(slots)
  for _, slot in ipairs(slots) do
    local c = self.cards[slot]
    add(out, proto.card(slot, c.icon, c.text))
  end
  return out
end

function M:disconnect() self.isConnected = false end
function M:connected() return self.isConnected end

function M:tick(now)
  local out = {}
  if not self.isConnected then return out end
  if now >= self.nextHello then
    add(out, proto.hello(self.version))
    self.nextHello = now + HELLO_PERIOD
  end
  if now >= self.nextTime then
    add(out, self:timeFrame())
    self.nextTime = now + TIME_PERIOD
  end
  return out
end

local function setLevel(self, field, codec, pct, muted)
  local new = { pct = round(pct), muted = muted and true or false }
  local old = self[field]
  self[field] = new
  local out = {}
  if self.isConnected and not (old and old.pct == new.pct and old.muted == new.muted) then
    add(out, codec(new.pct, new.muted))
  end
  return out
end

function M:setVolume(pct, muted) return setLevel(self, "volume", proto.volume, pct, muted) end
function M:setMic(pct, muted) return setLevel(self, "mic", proto.mic, pct, muted) end

function M:setCard(slot, icon, str)
  local clean = text.clean(str)
  local old = self.cards[slot]
  self.cards[slot] = { icon = icon, text = clean }
  local out = {}
  if self.isConnected and not (old and old.icon == icon and old.text == clean) then
    add(out, proto.card(slot, icon, clean))
  end
  return out
end

function M:onDeck(msg63)
  local m = proto.decodeDeck(msg63)
  if not m then return {} end
  local mic = self.mic or { pct = 0, muted = false }
  if m.type == "mic_delta" then
    return { { cmd = "mic_volume", pct = math.max(0, math.min(100, mic.pct + m.steps * MIC_STEP)) } }
  elseif m.type == "mic_mute" then
    return { { cmd = "mic_mute", muted = not mic.muted } }
  end
  return {}
end

function M:release(now)
  self.isConnected = false
  self.pausedUntil = now + RELEASE_PAUSE
end

function M:paused(now) return now < self.pausedUntil end

return M
