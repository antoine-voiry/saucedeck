# SAUCEDECK: LCD key legends, press feedback, status display via Hammerspoon helper

Status: design approved in brainstorming 2026-10-31, pending spec review.
Scope: firmware (ESP32-S3) + a Hammerspoon Lua helper on the Mac. No new hardware.
Builds on `2026-10-20-wifi-keymap-helper-design.md` (CDC framing, `HelperProto`, `HostLink`).

## 1. Goals

1. The LCD shows what each of the 12 keys does, laid out like the physical keys.
2. A pressed key gives visible feedback on its tile (and orange when its action failed).
3. The LCD shows Mac status: clock, next meeting, now playing, Mac stats, weather, unread counts.
4. Status comes from a Hammerspoon script over the existing USB CDC link. No Swift app needed.
5. Without the helper the deck still works: legends, volume, mic state, banners. Status area empties.

Non-goals: per-key displays, images/album art, Wi-Fi status fetch, idle screensaver, boot
animation, Windows, non-ASCII text on the LCD.

## 2. Decisions (with owner, 2026-10-31)

| Topic | Decision |
| --- | --- |
| Layout | Split: 4×3 legend grid on top, status strip at the bottom |
| Animation | Key press feedback only (tile fill + fade) |
| Status source | Mac helper over USB CDC, not Wi-Fi (Wi-Fi is off by default, Mac blocked at L2) |
| Helper | Hammerspoon Lua (`hs.serial`), replaces the planned Swift app for now |
| Status content | Generic text cards pushed by the helper; deck rotates them. New sources = Lua edit, no reflash |
| Labels | Stored on the deck with the key map (NVS), so legends work without the helper |

## 3. Screen map (320×240 landscape, rotation 3)

```
y0   ┌ 14:32  Standup in 12m            ●H ●W ┐ top bar, 20 px
y20  ├────────┬────────┬────────┬────────┤
     │ Screen │ Chrome │ Gmail  │Telegram│ row 1 = key positions 1–4
     ├────────┼────────┼────────┼────────┤ row 2 = 5–8
     │ Fusion │Settings│  Help  │YouTube │ row 3 = 9–12
     ├────────┼────────┼────────┼────────┤ tiles 80 × 56 px
     │  Hide  │iMessage│Weather │ Bambu  │
y188 ├────────┴────────┴────────┴────────┤
     │ VOL ▕██████████──────▏ 62%  MIC LIVE │ strip line 1 (y190–211)
     │ ♪ Daft Punk - Around the World  2/5 │ strip line 2 (y214–239)
y240 └──────────────────────────────────────┘
```

- **Top bar:** clock `HH:MM` (left), card slot 0 text (middle, e.g. next meeting), helper dot
  (green = present, grey = absent) and Wi-Fi dot (blue = on, hidden = off) on the right.
- **Tiles:** 1 px gap, label in `FreeSans9pt7b` (ships with Adafruit GFX), centred, max 2
  lines. 3 px left stripe in the type colour: Keystroke grey, App blue, Url green,
  Shortcut purple.
- **Strip line 1:** horizontal volume bar (120 px) + percent, then mic text `MIC LIVE`,
  `MIC MUTED` (red) or `MIC 45%` (helper present). Muted speaker = grey bar + red `MUTED`.
- **Strip line 2:** rotating status card (icon glyph + text + `n/m` index). Any banner
  (`BANNER_*`, existing) takes this line over while active, then the card returns.
- The old volume screen (vertical bar, speaker and mic icons) is removed.
- Full repaint only at boot (after splash) and on a key-map change: ~61 ms at 20 MHz
  (153 600 B × 8 / 20 MHz). Everything else is partial (one tile, one strip line, top bar).

Readability: 2" panel at 320 px ≈ 200 dpi. GFX built-in font size 1 (8 px ≈ 1 mm) is too
small; FreeSans9pt (~12 px cap-to-descender) is the minimum. Bench-check at arm's length.

## 4. Components (deck_core, all unit-tested)

### 4.1 Key labels (KeyMap v2)
- `KeyEntry` gains `char label[kKeyLabelMax + 1]`, `kKeyLabelMax = 16`, printable ASCII
  (0x20–0x7E) or empty.
- `KeyMap::setLabel(i, text)` validates like values (`TooLong`, `NotPrintable`).
- NVS blob version 2: `'SDKM', 2, 12 × [type, vlen, value, llen, label], CRC32`.
  Version 1 blobs still load (labels empty). Bad magic/version/CRC → defaults as today.
- `displayLabel(entry, buf)`: label if set, otherwise derived from the value: keystroke →
  as written (`F13`, `cmd+h`), Url → host (`mail.google.com`), App/Shortcut → value.
- Portal: 12 more fields `Key N label`. Preset loader takes `label` from the preset JSON.
- `kKeyLabelMax` keeps a staged `KeyMap` small; the vendor `ACTION` message is unchanged
  (labels never leave the deck).

### 4.2 wrapLabel
- `wrapLabel(text, maxWidthPx, widthFn, line1, line2)`: `widthFn(char)` injected (glyph
  advance), so the rule is host-testable without the font.
- Fits one line → line 1 only. Else split at the last space that fits; no space → hard cut.
  Line 2 overflow → truncate and end with `.` × 3 ("..." , ASCII).
- Glue passes `FreeSans9pt7b` advances and `maxWidthPx = 80 - 3 (stripe) - 6 (padding)`.

### 4.3 TileAnim (press feedback)
- Per tile: `Idle`, `Held`, `Fading(step 0..3)`. Colour = `Normal` or `Failed`.
- `onPress(i, failed)` → `Held` (fill = stripe colour, or orange when failed, label inverted).
- `onRelease(i)` → fade back to the idle look in 4 steps × 60 ms (blend factor 3/4, 2/4, 1/4, 0).
- `tick(nowMs)` returns a bitmask of tiles to repaint and each tile's blend level.
- "Failed" = action could not be sent (App/Url/Shortcut while helper absent, see 2026-10-20 §4.8).
- Cost: one tile = 80 × 56 × 2 B = 8 960 B ≈ 3.6 ms SPI at 20 MHz. Encoders count in PCNT
  (no lost detents); I2C key scan delayed by at most a few ms. Frame pacing stays at
  `kMinFrameMs = 30`; steps that fall between frames are merged.

### 4.4 Status messages (HelperProto additions, host → deck, display only)

| Type | Payload |
| --- | --- |
| `MSG_TIME` 0x04 | `epoch` u32 LE (UTC seconds), `tzOffsetMin` i16 LE (−720…+840) |
| `MSG_CARD` 0x05 | `slot` 0–7, `icon` 0–7, `ttlS` u16 LE (0 = until replaced), `tlen` 0–48, text |

- Text: printable ASCII only. Any byte outside 0x20–0x7E, `tlen` > 48, slot/icon/tz out of
  range → frame rejected (counted, like today). Helper transliterates accents before sending.
- Icons (drawn with GFX shapes, no bitmaps): 0 none, 1 music, 2 calendar, 3 cpu, 4 weather,
  5 mail, 6 chat, 7 battery.
- `tlen` = 0 clears the slot.
- Same rule as before: host → deck messages can only change what the LCD shows.

### 4.5 DeckClock
- `onTime(epoch, tz, nowMs)` stores a base; `hhmm(nowMs, buf)` renders local time.
- No `TIME` since boot → `--:--`. Clock keeps running after helper loss (millis drift is
  small over hours; helper resends every 60 s while present).

### 4.6 StatusBoard
- 8 slots `{icon, text, expiresMs}`. Slot 0 = top bar, slots 1–7 = rotation.
- Rotation: next non-empty, non-expired slot every 5 s; `n/m` counts live slots.
  No live slots → line 2 blank.
- Helper lost (`HostLink::present` false) → all slots cleared.
- Exposes dirty flags for top bar and line 2 (feeds the screen model).

### 4.7 ScreenModel v2 (ui_layout.h)
- State adds: 12 tile looks (label hash + type + anim level), clock text, slot-0 text,
  helper/Wi-Fi dots, current card. Dirty flags: `TILE(i)` mask, `TOPBAR`, `STRIP_VOL`,
  `STRIP_MIC`, `STRIP_LINE2`, `ALL`.
- Banner and card share line 2: banner wins while `banner != BANNER_NONE`.
- Horizontal bar keeps the existing partial-strip idea (`barDelta` along x instead of y).

## 5. src/ glue
- `display.cpp`: rewrite of the layout; draws from ScreenModel v2 dirty flags only. Adds
  `FreeSans9pt7b`. No logic beyond drawing.
- `main.cpp`: key press/release → `TileAnim`; `helper_link.cpp` routes `TIME`/`CARD` into
  `DeckClock` / `StatusBoard`.
- `wifi_portal.cpp`: label fields; preset loader reads `label`.
- `keymap_store.cpp`: blob v2.

## 6. Hammerspoon helper (`tools/mac/saucedeck_helper.lua`)

Loaded from `~/.hammerspoon/init.lua` next to the existing `hammerspoon.lua` key bindings.

- **Link:** `hs.serial` opens the first `/dev/cu.usbmodem*` matching the deck at 115200
  (never 1200 = bootloader touch). Frames: `A5 5A | 63-byte message | CRC16-CCITT LE`,
  same as `tools/helper_sim.py`. Reconnect on `hs.usb.watcher` attach; close on detach.
- **Keep-alive:** `HELLO` every 1 s. `TIME` on connect and every 60 s.
- **Audio:** `VOLUME` / `MIC` from `hs.audiodevice` watchers (default output/input), on
  change and on connect. Handles deck → Mac `MIC_DELTA` (±5 %/step) and `MIC_MUTE`.
- **Cards** (each source independent, failure = slot cleared, never crashes the script):

| Slot | Source | Refresh |
| --- | --- | --- |
| 0 | Next meeting: `icalBuddy` CLI (Homebrew) | 60 s |
| 1 | Now playing: `hs.spotify` / `hs.itunes` | on change / 5 s |
| 2 | CPU / RAM: `hs.host.cpuUsage`, `hs.host.vmStat` | 5 s |
| 3 | Battery: `hs.battery` (skip on desktop Macs) | 60 s |
| 4 | Weather: `https://wttr.in/?format=...` via `hs.http` | 15 min |
| 5 | Unread: dock badges via `lsappinfo` (Mail, Telegram) | 30 s |

- **Port release for flashing:** `saucedeck.release()` closes the port and pauses the
  helper for 60 s. A PlatformIO `extra_script` (pre-upload) runs `hs -c
  'saucedeck.release()'` when the `hs` CLI exists, so the 1200 bps touch can open the port.
- **Text hygiene:** strip accents (`é → e`), drop other non-ASCII, cut to 48 chars.
- The helper runs no command from the deck except the existing mic actions; key actions stay
  on the F-key + `hammerspoon.lua` path.

Unverified (spike first, pass/fail note in docs/kb/toolchain.md):
- `hs.serial` exists in the installed Hammerspoon and can open the TinyUSB CDC port at 115200.
- `icalBuddy` output parse; `lsappinfo` badge label for Telegram.
- `hs -c` CLI installed (`hs.ipc.cliInstall()`).

## 7. Data flows

**Legend edit:** portal → key value + label → staged → knob 1 confirm → NVS v2 → full repaint.

**Key press:** MCP scan → debounce → `TileAnim.onPress` (failed if action can't be sent) →
tile repaint → release → 4-step fade.

**Status:** Hammerspoon source → `CARD` frame → `decodeHost` → `StatusBoard` → line 2 /
top bar repaint. `TIME` → `DeckClock` → top bar once a minute.

## 8. Error handling
- Bad `TIME`/`CARD` frame → dropped, counted (existing `rejected`).
- Helper gone 3 s → cards cleared, helper dot grey, clock keeps running.
- Port held by the helper during upload → pre-upload script releases it; if `hs` missing,
  upload fails as today and docs say: quit Hammerspoon or run `saucedeck.release()`.
- Label missing/invalid in NVS v2 → entry loads with empty label → derived label shown.

## 9. Power and hardware
- No new parts, no pin changes. Backlight unchanged (19 mA on GPIO9, 1 mA margin, see
  CLAUDE.md): tile colours change content, not backlight current.
- SPI bus load rises only during fades (≤ 4 tile repaints per press).
- Flash: FreeSans9pt7b adds a few KB `[unverified: check build size]`; 3 MB app slot has room.

## 10. Security
- New host → deck messages are display-only, validated (length, range, ASCII), fuzz-tested.
- A local process that opens the CDC port can show fake text on the LCD. Accepted (owner):
  it could already fake volume/mic levels; it still cannot trigger keys or actions.
- Labels are stored and shown only; never typed, never sent to the Mac.
- Helper fetches wttr.in over HTTPS; no credentials anywhere.

## 11. Testing
- **Native (TDD):** label validation + blob v1→v2 migration + derived labels; `wrapLabel`
  (fits, split at space, hard cut, truncate); `TileAnim` (hold, fade steps, failed colour,
  press during fade); `TIME`/`CARD` decode incl. random-input fuzz; `DeckClock` (no time,
  tz offset, midnight wrap); `StatusBoard` (rotation, ttl expiry, clear slot, helper loss);
  ScreenModel v2 dirty flags.
- **Sim:** extend `SimLcd` to record `fillRect` calls and accept `setFont`. Scenarios: boot
  shows 12 legends; key press repaints only that tile and fades in 4 steps; fake helper
  sends `CARD` → line 2 text; helper silence 3 s → cards gone; banner over card and back.
- **Bench (not unit-testable):** legibility at arm's length, fade feel, no key/encoder misses
  during fades, Hammerspoon helper end to end, upload while the helper is running.

## 12. Order of work
1. Spike: `hs.serial` on the deck CDC port (pass/fail in kb).
2. Deck: KeyMap v2 labels + `wrapLabel` + new layout + `TileAnim`.
3. Deck: `TIME`/`CARD` + `DeckClock` + `StatusBoard`; `helper_sim.py` sends them.
4. Sim scenarios.
5. Hammerspoon helper + pre-upload release script + docs/mac-setup.md.
6. Bench session; log in docs/bringup/, facts into docs/kb/.
