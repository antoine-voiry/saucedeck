# Mac helper (Hammerspoon) Implementation Plan — Plan 3

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Hammerspoon helper (spec 2026-10-31 §6) that keeps the deck's helper link alive (HELLO, TIME, VOLUME, MIC), feeds status cards, handles deck mic commands, and releases the port before a PlatformIO upload.

**Architecture:** All logic lives in pure Lua modules under `tools/mac/saucedeck/` (no `hs.*` calls) and is unit-tested on the host with Lua 5.4 (the version embedded in Hammerspoon). `tools/mac/saucedeck_helper.lua` is thin `hs.*` glue, smoke-tested against a fake `hs` table. A PlatformIO pre-upload script asks the helper to release the port via the `hs` CLI.

**Tech Stack:** Lua 5.4 (`/opt/homebrew/opt/lua@5.4/bin/lua`), Hammerspoon 1.1.1, Python 3 (PlatformIO penv) for the pre-upload script.

## Global Constraints

- Host Lua for tests: `/opt/homebrew/opt/lua@5.4/bin/lua` (Hammerspoon 1.1.1 embeds Lua 5.4.7; `lua` on PATH is 5.5, do not use it). Run all Lua tests: `/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua` from the repo root. Output ends with `N passed, 0 failed`; exit code non-zero on any failure.
- Frame format (must match `lib/deck_core/src/frame.h` and `tools/helper_sim.py` byte for byte): `A5 5A | 63-byte message | CRC16-CCITT-FALSE little-endian over the 63 bytes`. Message = `[ver=1, type, len, payload..., zero padding]`, payload ≤ 60 bytes. Frame size 67.
- Host→deck types: HELLO 0x01 `[helperVer]`, VOLUME 0x02 `[pct 0..100, muted 0/1]`, MIC 0x03 `[pct, muted]`, TIME 0x04 `[u32 epoch LE, i16 tzMin LE]` tz −720..840, CARD 0x05 `[slot 0..7, icon 0..7, ttl u16 LE, tlen ≤48, text]` text printable ASCII 0x20–0x7E. Deck→host: ACTION 0x81 `[key, kind, vlen, value]`, MIC_DELTA 0x82 `[int8 steps, −8..8, ≠0]`, MIC_MUTE 0x83 `[]`.
- Icons: none 0, music 1, calendar 2, cpu 3, weather 4, mail 5, chat 6, battery 7.
- Card slots: 0 next meeting (top bar), 1 now playing, 2 CPU/RAM, 3 battery, 4 weather, 5 unread.
- Timing: HELLO every 1 s; TIME on connect and every 60 s; deck drops the helper after 3 s silence.
- Mic step = 5 % per MIC_DELTA step, clamped 0..100.
- Release: `saucedeck.release()` closes the port and pauses the helper 60 s.
- Text hygiene: accents → ASCII (`é → e`), other non-ASCII dropped, control chars → space, spaces collapsed, trimmed, cut to 48 chars.
- The helper never acts on ACTION messages (key actions stay on the F-key + `hammerspoon.lua` path); it only handles MIC_DELTA and MIC_MUTE.
- Serial port: `/dev/cu.usbmodem*` only (a CH9102 board may sit on the desk), 115200 baud, never 1200 (bootloader touch).
- Pure modules (`tools/mac/saucedeck/*.lua`) must not reference the global `hs`.
- Code style: 2-space indent, `local M = {}` … `return M` modules, short comments only where the why is not obvious.

---

### Task 1: Lua test runner + `proto.lua` (frames, reader, deck decode)

**Files:**
- Create: `test/lua/run.lua`, `test/lua/test_proto.lua`, `tools/mac/saucedeck/proto.lua`

**Interfaces — Produces:**
- `proto.crc16(s) -> int`; `proto.frame(mtype, payloadStr) -> 67-byte string | nil` (nil if payload > 60)
- `proto.hello(ver)`, `proto.volume(pct, muted)`, `proto.mic(pct, muted)`, `proto.time(epoch, tzMin)`, `proto.card(slot, icon, text, ttl)` → frame string or nil when out of range (pct is rounded and clamped 0..100; `muted` is a boolean; tz outside −720..840, slot/icon outside 0..7, text > 48 or non-printable → nil; ttl defaults 0)
- `proto.ICONS` table (names above); type constants `proto.HELLO … proto.MIC_MUTE`
- `proto.reader()` → object with `:push(bytes) -> {msg63, ...}` (re-syncs on junk and bad CRC like `FrameReader` in frame.h)
- `proto.decodeDeck(msg63) -> {type="mic_delta", steps=n} | {type="mic_mute"} | {type="action", key=k, kind=t, value=v} | nil`

- [ ] **Step 1: test runner** `test/lua/run.lua`:

```lua
-- Host test runner for the Hammerspoon helper's pure modules (Lua 5.4).
-- Usage (repo root): /opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua
package.path = "tools/mac/?.lua;test/lua/?.lua;" .. package.path

local passed, failed = 0, 0
local T = {}

function T.eq(got, want, msg)
  if got ~= want then
    error((msg or "eq") .. ": got " .. tostring(got) .. ", want " .. tostring(want), 2)
  end
end
function T.ok(v, msg) if not v then error(msg or "expected truthy", 2) end end
function T.hex(s) return (s:gsub(".", function(c) return string.format("%02x", c:byte()) end)) end

local suites = { "test_proto" }
for _, name in ipairs(arg[1] and { arg[1] } or suites) do
  local suite = require(name)
  local names = {}
  for k in pairs(suite) do names[#names + 1] = k end
  table.sort(names)
  for _, k in ipairs(names) do
    local ok, err = pcall(suite[k], T)
    if ok then passed = passed + 1 else
      failed = failed + 1
      print("FAIL " .. name .. "." .. k .. ": " .. tostring(err))
    end
  end
end
print(passed .. " passed, " .. failed .. " failed")
os.exit(failed == 0 and 0 or 1)
```

Each test file returns a table of `function(T)` tests. Later tasks append their suite name to `suites`.

- [ ] **Step 2: failing tests** `test/lua/test_proto.lua` (golden vectors generated by `tools/helper_sim.py` `frame()` / `card_frame()` on 2026-10-31):

```lua
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
```


- [ ] **Step 3: run, confirm RED** — `/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua` → fails with `module 'saucedeck.proto' not found`.

- [ ] **Step 4: implement** `tools/mac/saucedeck/proto.lua`. Use `string.pack("<I4i2", epoch, tz)` for TIME, `string.pack("<I2", ttl)` for CARD ttl, `math.floor(pct + 0.5)` for rounding. CRC exactly as `crc16ccitt` in frame.h (poly 0x1021, init 0xFFFF, no reflection, no final xor). Reader: buffer string, scan for `A5 5A`, wait for 67 bytes, CRC check, on bad CRC drop one byte and rescan. `decodeDeck`: length 63, byte1 == 1, `len <= 60`; MIC_DELTA needs len 1 and signed step in −8..8, ≠ 0; MIC_MUTE len 0; ACTION len ≥ 3 and `3 + vlen == len`.

- [ ] **Step 5: run, GREEN** — `… 18 passed, 0 failed` (count = number of tests in the file).

- [ ] **Step 6: kb + commit.** Append to `docs/kb/toolchain.md` under a new `## Hammerspoon helper (2026-10-31)` heading:
  - `Hammerspoon 1.1.1 embeds Lua 5.4.7 (LuaSkin.framework strings). Host tests use Homebrew lua@5.4 (keg-only, /opt/homebrew/opt/lua@5.4/bin/lua); \`lua\` on PATH is 5.5. [verified: strings + lua -v, 2026-10-31]`
  - `\`hs\` CLI is at /opt/homebrew/bin/hs but answers only when the Hammerspoon config loads \`hs.ipc\` ("can't access Hammerspoon message port" otherwise). [verified: hs -c, 2026-10-31]`

```bash
git add test/lua tools/mac/saucedeck/proto.lua docs/kb/toolchain.md
git commit -m "Mac helper: Lua frame/protocol module with host tests matching helper_sim

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: `text.lua` + `cards.lua` (hygiene and card formatters)

**Files:**
- Create: `tools/mac/saucedeck/text.lua`, `tools/mac/saucedeck/cards.lua`, `test/lua/test_text.lua`, `test/lua/test_cards.lua`
- Modify: `test/lua/run.lua` (suites list += `"test_text", "test_cards"`)

**Interfaces — Produces:**
- `text.clean(s) -> string` (always a string, possibly empty, ≤ 48 chars, printable ASCII only; `nil` input → `""`)
- `cards.meeting(icalOut) -> string|nil` — first non-empty line, bullets (`•`, `*`, `-` prefix) stripped, cleaned; nil when empty
- `cards.nowPlaying(artist, title, playing) -> string|nil` — `"Artist - Title"`, just title when artist empty; nil when not playing or no title
- `cards.stats(cpuPct, ramPct) -> string` — `"CPU 12% RAM 61%"` (rounded)
- `cards.ramPercent(vm) -> number|nil` — `(pagesActive + pagesWiredDown + pagesUsedByVMCompressor) * pageSize / memSize * 100`, nil if any field missing or memSize 0
- `cards.battery(pct, charging) -> string|nil` — `"Battery 87%"` / `"Battery 87% charging"`; nil when pct nil (desktop Mac)
- `cards.weather(body) -> string|nil` — first line cleaned; nil when empty, contains `<` (HTML error page) or starts with `Unknown`
- `cards.badge(lsappinfoOut) -> int` — `"label"="7"` → 7; a non-numeric non-empty label → 1; otherwise 0
- `cards.unread(list) -> string|nil` — list of `{name, count}` in order; `"Mail 3 Telegram 2"` skipping zeros; nil when all zero

All results pass through `text.clean` (so ≤ 48 chars).

- [ ] **Step 1: failing tests** `test/lua/test_text.lua`:

```lua
local X = require("saucedeck.text")
local t = {}
t.plain_ascii_kept = function(T) T.eq(X.clean("Standup 10:30"), "Standup 10:30") end
t.nil_is_empty = function(T) T.eq(X.clean(nil), "") end
t.accents_folded = function(T)
  T.eq(X.clean("Caf\xC3\xA9 cr\xC3\xA8me \xC3\xA0 Gen\xC3\xA8ve"), "Cafe creme a Geneve")
  T.eq(X.clean("\xC3\x89t\xC3\xA9 \xC3\x87a \xC3\xB1 \xC3\xBC\xC3\xB6\xC3\xA4\xC3\xAE\xC3\xB4\xC3\xBB\xC3\xAA\xC3\xAB\xC3\xAF"), "Ete Ca n uoaioueei")
end
t.punctuation_mapped = function(T)
  T.eq(X.clean("It\xE2\x80\x99s \xE2\x80\x93 ok\xE2\x80\xA6"), "It's - ok...")
end
t.degree_and_emoji_dropped = function(T)
  T.eq(X.clean("Patchy rain nearby +13\xC2\xB0C"), "Patchy rain nearby +13C")
  T.eq(X.clean("\xF0\x9F\x8E\xB5 Song"), "Song")
end
t.control_chars_become_space_and_collapse = function(T)
  T.eq(X.clean("  a\tb\n\nc\r  "), "a b c")
end
t.cut_to_48 = function(T)
  T.eq(#X.clean(string.rep("y", 60)), 48)
end
t.unsafe_bytes_dropped = function(T) T.eq(X.clean("a\x7Fb\xFFc"), "abc") end
return t
```

`test/lua/test_cards.lua`:

```lua
local C = require("saucedeck.cards")
local t = {}
t.meeting_first_line = function(T)
  T.eq(C.meeting("\n\xE2\x80\xA2 14:30 Standup \xC3\xA9quipe\n    notes\n"), "14:30 Standup equipe")
  T.eq(C.meeting("* 09:00 Review\n"), "09:00 Review")
  T.eq(C.meeting("\n  \n"), nil); T.eq(C.meeting(nil), nil)
end
t.now_playing = function(T)
  T.eq(C.nowPlaying("Daft Punk", "One More Time", true), "Daft Punk - One More Time")
  T.eq(C.nowPlaying("", "Podcast", true), "Podcast")
  T.eq(C.nowPlaying("A", "B", false), nil); T.eq(C.nowPlaying("A", nil, true), nil)
end
t.stats_rounded = function(T) T.eq(C.stats(12.4, 60.6), "CPU 12% RAM 61%") end
t.ram_percent = function(T)
  local vm = { pagesActive = 100, pagesWiredDown = 50, pagesUsedByVMCompressor = 50, pageSize = 16384, memSize = 16384 * 400 }
  T.eq(C.ramPercent(vm), 50)
  T.eq(C.ramPercent({ pageSize = 1 }), nil)
  vm.memSize = 0; T.eq(C.ramPercent(vm), nil)
end
t.battery = function(T)
  T.eq(C.battery(87, false), "Battery 87%"); T.eq(C.battery(86.6, true), "Battery 87% charging")
  T.eq(C.battery(nil, false), nil)
end
t.weather = function(T)
  T.eq(C.weather("Patchy rain nearby +13\xC2\xB0C\n"), "Patchy rain nearby +13C")
  T.eq(C.weather("<html>503</html>"), nil); T.eq(C.weather("Unknown location"), nil)
  T.eq(C.weather(""), nil); T.eq(C.weather(nil), nil)
end
t.badge = function(T)
  T.eq(C.badge('"StatusLabel"={ "label"="7" }'), 7)
  T.eq(C.badge('"StatusLabel"={ "label"="\xE2\x80\xA2" }'), 1)
  T.eq(C.badge('"StatusLabel"=[ NULL ]'), 0); T.eq(C.badge(""), 0); T.eq(C.badge(nil), 0)
end
t.unread = function(T)
  T.eq(C.unread({ { "Mail", 3 }, { "Telegram", 2 } }), "Mail 3 Telegram 2")
  T.eq(C.unread({ { "Mail", 0 }, { "Telegram", 5 } }), "Telegram 5")
  T.eq(C.unread({ { "Mail", 0 } }), nil)
end
t.all_outputs_fit = function(T) T.ok(#C.nowPlaying(string.rep("a", 40), string.rep("b", 40), true) <= 48) end
return t
```

- [ ] **Step 2: RED** — run `/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua` → module-not-found failures for the two new suites; test_proto stays green.

- [ ] **Step 3: implement.** `text.lua`: a table mapping 2- and 3-byte UTF-8 sequences to ASCII (À-ÿ Latin-1 letters by family: a, c, e, i, n, o, u, y with upper case; `’‘` → `'`, `“”` → `"`, `–—` → `-`, `…` → `...`, `°` → empty), apply with `gsub` on UTF-8 sequences (`"[\xC2-\xF4][\x80-\xBF]*"`), then control chars (0x00–0x1F) → space, drop bytes ≥ 0x7F, collapse spaces, trim, `sub(1, 48)` then trim trailing space again. `cards.lua` requires `saucedeck.text`.

- [ ] **Step 4: GREEN**, all suites: `… passed, 0 failed`.

- [ ] **Step 5: commit**

```bash
git add tools/mac/saucedeck/text.lua tools/mac/saucedeck/cards.lua test/lua
git commit -m "Mac helper: text hygiene and status card formatters (host tested)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: `core.lua` — helper state machine (hardware-free)

**Files:**
- Create: `tools/mac/saucedeck/core.lua`, `test/lua/test_core.lua`
- Modify: `test/lua/run.lua` (suites += `"test_core"`)

**Interfaces — Consumes:** `saucedeck.proto`, `saucedeck.text`.
**Produces** (`now` = monotonic seconds, float):
- `core.new{clock = function() return epoch, tzMin end, version = 1}` → object
- `c:pickPort(names) -> string|nil` — first name (sorted) starting with `usbmodem` (names as given by `hs.serial.availablePortNames()`, e.g. `usbmodem288485692A0C2`); ignores `usbserial*`, `wchusbserial*`, `Bluetooth*`
- `c:connect(now) -> {frames}` — HELLO, TIME, VOLUME and MIC if known, then every non-empty card ordered by slot; schedules next HELLO at now+1, next TIME at now+60
- `c:disconnect()`; `c:connected() -> bool`
- `c:tick(now) -> {frames}` — HELLO when due (then +1 s), TIME when due (then +60 s); `{}` when disconnected
- `c:setVolume(pct, muted) -> {frames}` / `c:setMic(pct, muted)` — remembers value; returns one frame when connected and the (rounded pct, muted) pair changed, else `{}`
- `c:setCard(slot, icon, text) -> {frames}` — text through `text.clean` (`nil` → clear); remembers; frame only when connected and (icon, cleaned text) changed
- `c:onDeck(msg63) -> {commands}` — uses `proto.decodeDeck`: mic_delta → `{cmd="mic_volume", pct=clamp(micPct + steps*5, 0, 100)}`; mic_mute → `{cmd="mic_mute", muted=not micMuted}`; action / invalid → `{}`. Mic unknown → base pct 0, muted false.
- `c:release(now)` — disconnects, paused until now+60; `c:paused(now) -> bool`

- [ ] **Step 1: failing tests** `test/lua/test_core.lua`:

```lua
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
```

- [ ] **Step 2: RED** — new suite fails with module not found; others green.
- [ ] **Step 3: implement** `core.lua` (metatable class; pure; no `hs`).
- [ ] **Step 4: GREEN** all suites.
- [ ] **Step 5: commit**

```bash
git add tools/mac/saucedeck/core.lua test/lua
git commit -m "Mac helper: link state machine (keep-alive, dedupe, mic commands, release)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Hammerspoon glue + pre-upload release + docs

**Files:**
- Create: `tools/mac/saucedeck_helper.lua`, `test/lua/test_glue.lua`, `test/lua/fake_hs.lua`, `tools/release_helper.py`, `test/tools/test_release_helper.py`
- Modify: `test/lua/run.lua` (suites += `"test_glue"`), `platformio.ini` (`[env:saucedeck]` and `[env:bringup]`: `extra_scripts = pre:tools/release_helper.py`), `docs/mac-setup.md`, `CLAUDE.md` (Commands block: Lua + release test lines), `docs/kb/toolchain.md`

**Interfaces — Consumes:** `core`, `cards`, `proto` from Tasks 1–3.

**Glue design** (`saucedeck_helper.lua`, loaded with `dofile`; sets `package.path` to include `<dir of this file>/?.lua` so `require("saucedeck.core")` works):
- `require("hs.ipc")` so the `hs` CLI works.
- Global `saucedeck = { release = fn, status = fn -> string, stop = fn }`. `stop()` stops timers/watchers and closes the port (used by the smoke test and by reloads).
- 1 s timer: if not connected and not paused → `pickPort(hs.serial.availablePortNames())` → `hs.serial.newFromName(name):baudRate(115200):callback(onSerial):open()`; on success write `core:connect(now)`. When connected → write `core:tick(now)`. Write errors / `"removed"` callback → `core:disconnect()`, close.
- `onSerial(port, msg, data)`: on `"received"` → `reader:push(data)` → for each msg `core:onDeck` → run commands on `hs.audiodevice.defaultInputDevice()` (`setInputVolume(pct)`, `setInputMuted(b)`), then `core:setMic` with the new value and write.
- 0.2 s audio timer: default output device `outputVolume()`/`outputMuted()` → `core:setVolume`; default input `inputVolume()`/`inputMuted()` → `core:setMic`; write returned frames. (Spec says watchers; polling at 0.2 s is the deliberate simpler choice, dedupe makes it free on the wire.)
- Card sources, each wrapped in `pcall`; error or nil → `core:setCard(slot, icon, nil)`: slot 0 `hs.task` `icalBuddy -n -nc -nrd -ea -li 1 -b "" -iep "datetime,title" -po "datetime,title" -df "" -tf "%H:%M" eventsToday` every 60 s (missing binary → clear); slot 1 `hs.spotify` then `hs.itunes` (`isPlaying`, `getCurrentArtist`, `getCurrentTrack`) every 5 s; slot 2 `hs.host.cpuUsage().overall.active` + `cards.ramPercent(hs.host.vmStat())` every 5 s; slot 3 `hs.battery.percentage()`/`isCharging()` every 60 s; slot 4 `hs.http.asyncGet("https://wttr.in/?format=%C+%t", nil, cb)` every 15 min; slot 5 `hs.task` `lsappinfo info -only StatusLabel <App>` for Mail and Telegram every 30 s → `cards.badge` → `cards.unread`.
- `release()`: `core:release(now)`, close port, `hs.alert.show("SAUCEDECK helper paused 60 s")`.

- [ ] **Step 1: failing tests.** `test/lua/fake_hs.lua` returns a constructor for a fake `hs` table recording calls: `hs.serial.availablePortNames()` (returns configurable list), `newFromName` → port object with chainable `baudRate/callback/open/close/sendData` (records sent bytes), `hs.timer.doEvery(sec, fn)` (records fn, returns object with `stop`), `hs.timer.secondsSinceEpoch()`, `hs.timer.absoluteTime()` (ns), `hs.audiodevice.defaultOutputDevice/defaultInputDevice` (objects with volume/mute getters and setters), `hs.alert.show`, `hs.task.new` (records, `start` no-op), `hs.http.asyncGet`, `hs.spotify`/`hs.itunes` (not playing), `hs.host.cpuUsage/vmStat`, `hs.battery.percentage` → nil, `hs.usb.watcher.new` → `{start, stop}`, and `package.preload["hs.ipc"]` = function returning `{}`. `test/lua/test_glue.lua`:
  - `loads_and_exposes_api`: install fake as global `hs`, `dofile("tools/mac/saucedeck_helper.lua")`, assert `type(saucedeck.release) == "function"` and `saucedeck.status` returns a string; `saucedeck.stop()`; clear globals.
  - `connects_on_timer_and_sends_hello`: ports `{"usbmodemX"}`, run the 1 s timer fn once → port opened at 115200, first sent frame equals `proto.hello(1)`.
  - `mic_delta_from_deck_sets_input_volume`: after connect, input device at 50 %; call the recorded serial callback with `"received"` and a MIC_DELTA +2 frame → fake input device volume == 60 and a MIC frame with 60 was sent.
  - `release_closes_and_pauses`: `saucedeck.release()` → port closed; timer fn again → no new open.
  - Each test calls `saucedeck.stop()` and resets `_G.hs`/`_G.saucedeck` at the end.

  `test/tools/test_release_helper.py` (unittest, run with `~/.platformio/penv/bin/python -m unittest discover -s test/tools`):
  - `release(which=lambda n: None, run=fail)` returns False and never calls `run`.
  - with `which` returning `/opt/homebrew/bin/hs` it calls `run(["/opt/homebrew/bin/hs", "-t", "2", "-c", "saucedeck.release()"], timeout=5, capture_output=True)` and returns True when returncode 0.
  - non-zero returncode → False; `run` raising `subprocess.TimeoutExpired` or `OSError` → False (never raises).

- [ ] **Step 2: RED** — glue suite and python test fail (missing files).
- [ ] **Step 3: implement** glue and `tools/release_helper.py`:

```python
"""PlatformIO pre-upload hook: ask the Hammerspoon helper to free the deck's serial port.

The 1200 bps touch that resets the deck into its bootloader cannot open the port while the
helper holds it. Missing `hs` CLI or helper -> upload proceeds as before.
"""
import shutil, subprocess, time


def release(which=shutil.which, run=subprocess.run):
    hs = which("hs")
    if not hs:
        return False
    try:
        r = run([hs, "-t", "2", "-c", "saucedeck.release()"], timeout=5, capture_output=True)
    except (subprocess.TimeoutExpired, OSError):
        return False
    return r.returncode == 0


try:
    Import("env")  # noqa: F821 (SCons global, only defined inside PlatformIO)
except NameError:
    env = None

if env is not None:
    def _before_upload(source, target, env):
        if release():
            print("release_helper: Hammerspoon helper released the port")
            time.sleep(0.5)

    env.AddPreAction("upload", _before_upload)
```

- [ ] **Step 4: GREEN** — all Lua suites pass; `python -m unittest discover -s test/tools` OK; `pio test -e native` green; `pio run -e saucedeck -e bringup` SUCCESS, no new warnings (the extra_script must not print anything during a plain build).
- [ ] **Step 5: docs.**
  - `docs/mac-setup.md`: new section "Status helper" — `init.lua` line `dofile(os.getenv("HOME") .. "/saucedeck/tools/mac/saucedeck_helper.lua")`; optional `brew install ical-buddy` (meeting card) and Calendar access prompt; what each card slot shows; upload releases the port automatically when the `hs` CLI exists, else quit Hammerspoon or run `saucedeck.release()` in its console; running the tests (`/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua`).
  - `CLAUDE.md` Commands block: add `/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua   # Mac helper (Hammerspoon) logic tests` and `~/.platformio/penv/bin/python -m unittest discover -s test/tools   # pre-upload release hook`.
  - `docs/kb/toolchain.md` (Hammerspoon section): `[unverified]` entries for `hs.serial` opening the TinyUSB CDC port, `icalBuddy` output format, `lsappinfo` StatusLabel format for Telegram/Mail, `hs.host.vmStat` field names — to be checked on the bench.
- [ ] **Step 6: commit**

```bash
git add tools/mac/saucedeck_helper.lua tools/release_helper.py test/lua test/tools platformio.ini docs/mac-setup.md CLAUDE.md docs/kb/toolchain.md
git commit -m "Mac helper: Hammerspoon glue, pre-upload port release, setup docs

Not bench-verified: needs the helper loaded in Hammerspoon and a deck on USB.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5 (operator, not a subagent): bench

Load the helper in `~/.hammerspoon/init.lua` (user's file — ask first), reload, then check: helper dot green, clock right, VOLUME follows Mac volume keys, knob 2 push mutes the Mac mic, knob 2 push+turn changes Mac input level, cards appear, quitting Hammerspoon clears cards within 3 s, `pio run -t upload` succeeds with the helper running. Log `docs/bringup/YYYY-MM-DD.log`; move verified facts from `[unverified]` to `[verified: bench]` in kb.
