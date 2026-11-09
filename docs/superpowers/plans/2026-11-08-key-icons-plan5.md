# Key icons Implementation Plan — Plan 5

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Each LCD key tile shows a 24×24 monochrome icon above a one-line label, chosen per key by name (preset / portal). Keys without an icon keep the Candy text tile.

**Spec:** `docs/superpowers/specs/2026-11-08-key-icons-design.md` (read it; this plan does not repeat every detail).

**Architecture:** Icon art + lookup + run extraction are pure (`lib/deck_core/src/icons.h`). The key map stores an icon id (blob v3). `src/display.cpp` draws icon runs with `drawFastHLine`. The sim records/rasterizes hlines so scenario tests and the BMP screenshot cover the real drawing code.

**Tech stack:** C++17, Unity (native + sim envs), PlatformIO pioarduino, Adafruit GFX 1.12.6, Python 3 + Pillow for the icon preview tool.

## Global Constraints

- Icons: 20, ids 1..20, 0 = none. Names in this exact id order: `camera globe envelope plane cube gear question play eye-off chat weather printer mic music folder terminal lock moon bolt home`. Lowercase, ≤ 10 chars. `kIconCount = 20`, `kIconSize = 24`.
- Art: 24 strings × 24 chars, `#` ink, `.` empty; outermost 1 px ring empty; 2 px strokes, about 2 px margin, centred.
- API (icons.h): `uint8_t iconByName(const char *name)` (case-insensitive exact; null/empty/unknown → 0); `const char *iconName(uint8_t id)` (`""` for 0 / out of range); `struct IconRun { uint8_t x, len; }; uint8_t iconRuns(uint8_t id, uint8_t row, IconRun *out, uint8_t cap)` (0 for bad id or row ≥ 24; at most cap runs).
- KeyEntry gains `uint8_t icon` (default 0). `KeyMapError::BadIcon` (appended last in the enum). `KeyMap::setIcon(uint8_t i, const char *name)`: empty/null → 0 ok, unknown → BadIcon, bad index → BadIndex. `operator==` compares icon.
- NVS blob v3: `'SDKM', 3, 12 × [type, vlen, value, llen, label, icon], CRC32 LE`; v1/v2 still load with icon 0; icon id > 20 → reject. `kKeyMapBlobMax` + 12.
- Portal: `kFields` = 4 (type, value, label, icon); icon field id `i<n>`, label `Key n icon`, length 10, value `iconName(entry.icon)`. Bad icon name on Save → that key's "Invalid key n" banner. Preset JS: optional `"icon"` string ≤ 10 chars (else `key n: bad icon`), fills `i<n>` or `""`. One line under the preset loader lists the valid icon names.
- Preset `presets/mac-daily.json` labels/icons, keys 1..12: Screenshot/camera, Chrome/globe, Gmail/envelope, Telegram/plane, Fusion 360/cube, Settings/gear, Help/question, YouTube/play, Hide app/eye-off, iMessage/chat, Weather/weather, Bambu/printer. Types and values unchanged. Field order per key stays `label, type, value`, then `icon` last.
- Tile (Plan 4 geometry unchanged: x = c*80+3, y = 24+r*54, 74×50, r8, band 5): icon box at (x+25, y+8) 24×24, runs drawn `drawFastHLine(x+25+run.x, y+8+row, run.len, look.text)`; label one line, baseline y+44, centred in 74 px, max 72 px: `FreeSans7pt7b` regular only, `fitLine` cut with "..." past 72 px (revision 2: bold 7pt loses the i/j dot) (revised 2026-11-08: 9pt / 68 px / y+45 cut 6 of 12 labels). 7pt fonts generated with Adafruit fontconvert (DPI 141) from GNU FreeFont 20120503, committed in `include/fonts/`, rebuild script `tools/make_label_fonts.sh`. Text tile (icon 0) unchanged.
- TDD for every deck_core change; `src/` stays glue. Done gate per task: `~/.platformio/penv/bin/pio test -e native` green, `~/.platformio/penv/bin/pio test -e sim` green, `/opt/homebrew/opt/lua@5.4/bin/lua test/lua/run.lua` green, `~/.platformio/penv/bin/pio run -e saucedeck -e bringup` SUCCESS, zero new warnings. No uploads in Tasks 1–3.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Branch `lcd-legends-status`; never commit to main.

---

### Task 1: `icons.h` — art, lookup, runs (+ preview tool)

**Files:** create `lib/deck_core/src/icons.h`, `test/native/test_icons/test_main.cpp`, `tools/icons_preview.py`.

- [ ] Tests first (new suite `test_icons`), RED for the right reason (missing header), then GREEN:
  - for every id 1..20: 24 rows, each exactly 24 chars, only `#`/`.`; ≥ 1 ink pixel; row 0, row 23, column 0 and column 23 all `.`;
  - names: unique, ≤ 10 chars, lowercase, in the Global Constraints order (`iconName(1)` = "camera", `iconName(20)` = "home");
  - `iconByName`: "camera" → 1, "CAMERA" → 1, "Eye-Off" → 9, "came" → 0, "" → 0, nullptr → 0, "camera " → 0;
  - `iconName(0)` and `iconName(21)` → "";
  - `iconRuns`: a row taken from a test-local copy is not possible (art is data) — instead check runs against the art itself: for every id and row, rebuilding a 24-char string from the runs equals the stored row; plus fixed cases: id 0 → 0 runs, row 24 → 0 runs, id 21 → 0 runs, cap smaller than the run count → returns cap and only the first runs.
- [ ] Implement icons.h: art as `static const char *const` row tables (one per icon, readable), names table, the three functions. No Arduino.h, no heap.
- [ ] Art: draw all 20 recognisably (camera body + lens; globe circle + meridian/parallels; envelope with flap V; paper plane; isometric cube; gear with teeth; question mark; play triangle in rounded rect; eye with slash; speech bubble with tail; sun behind cloud; 3D printer frame + nozzle + bed; microphone; music note pair; folder; terminal window with `>_`; padlock; crescent moon; lightning bolt; house).
- [ ] `tools/icons_preview.py <out.png>`: parses icons.h row strings, renders a 5×4 contact sheet at 6× scale with names under each icon (Pillow). Run it, Read the PNG yourself, fix any icon that does not read clearly at a glance. Put the PNG in the report path folder (not in the repo).
- [ ] Done gate, commit: "Icons: 20 built-in 24x24 key icons (ASCII art, lookup, runs)".

### Task 2: Key map icon field, blob v3, portal field, preset

**Files:** `lib/deck_core/src/keymap.h`, `lib/deck_core/src/keymap.cpp`, `test/native/test_keymap/test_main.cpp`, `src/wifi_portal.cpp`, `presets/mac-daily.json`, `test/sim/lib/simdeck/src/sim_deck.cpp` (preset loader reads optional `"icon"` after `"value"` within the same key object; missing → none), sim test for the portal if `test_sim_*` already covers portal Save (extend it: bad icon name → invalid key banner; good name → staged map has the icon).

- [ ] Tests first: `setIcon` (known, case-insensitive, empty → 0, unknown → BadIcon, index 12 → BadIndex); default icons 0; `==` differs when only an icon differs; v3 round-trip keeps icons; a v2 blob (build it by hand in the test: magic, 2, entries without icon byte, CRC) loads with icons 0; v3 blob with icon byte 21 (CRC recomputed) → `deserializeKeyMap` false; existing v1 test stays green.
- [ ] Implement; bump `kBlobVersion` to 3 with a comment (v3 2026-11-08 adds icons).
- [ ] Portal glue + JS + icon names line. Sim loader. Preset JSON per Global Constraints.
- [ ] Done gate, commit: "Key map: per-key icon (blob v3, portal field, preset icons)".

### Task 3: Icon tiles on the LCD + sim + screenshot

**Files:** `test/sim/lib/simdeck/src/Adafruit_ST7789.h` + `fakes.cpp` + `sim/lcd.h` (`drawFastHLine(x, y, w, c)`: record in a new `hlines` vector as SimLcdRect with h = 1, rasterize with clipping), `src/display.cpp`, `test/native/test_sim_display/test_main.cpp`, `test/native/test_sim_shot/test_main.cpp` (only if needed for the preset), `docs/superpowers/specs/assets/2026-11-08-icons-sim.png`, `docs/kb/toolchain.md` (flash size line).

- [ ] (Dropped in revision 2: label = `fitLine` in regular 7pt, no pure helper needed. Original step:) `chooseOneLine(const char *text, uint16_t maxPx, GlyphWidthFn bold, GlyphWidthFn regular, LegendFont &font, char *out, size_t cap)` tests first with the suite's fake widths (bold 9, regular 8, space 4, max 68): "Chrome" → Bold, "Chrome"; an 8-char word (bold 72 > 68, regular 64) → Regular whole; "Fusion 360 Studio" → Regular, cut ending "...", width ≤ 68; "" → Bold "".
- [ ] Sim primitive test (hline pixels + clipping) first, then the fake.
- [ ] Sim scenarios first (RED against current display): with the mac-daily preset loaded, tile 1 records hlines all inside (tileX(1)+25, tileY(1)+8, 24, 24) and prints "Screenshot" exactly at baseline tileY(1)+45; pressing key 8 to full level draws its hlines in black (0x0000); a key map with icon 0 on key 3 still prints its two-line / one-line legend as before (existing tests stay green).
- [ ] display.cpp: icon branch in `drawTile` per Global Constraints.
- [ ] Generate `.pio/shots/deck.bmp` via the shot scenario, convert with `sips -s format png` to `docs/superpowers/specs/assets/2026-11-08-icons-sim.png`. Read it; list in the report anything that looks off.
- [ ] Done gate (+ flash size into kb toolchain line), commit: "LCD: icon tiles (24x24 icon above one-line label)" + "Not bench-verified."

### Task 4 (controller + owner): review screenshot, flash, look

Owner reviews the sim PNG first. Then upload (`--upload-port` explicit if needed), owner reloads `presets/mac-daily.json` through the portal (hold both knobs 5 s → saucedeck.local/param → Load preset → Save → press knob 1), checks icons at arm's length and press feedback.
