# Key icons — design

Date: 2026-11-08. Status: approved in chat (owner picked: built-in symbol set, icon above a one-line label, mapping below).

## 1. Goal

Each LCD key tile shows a monochrome 24×24 icon above a short one-line label, instead of text only. Keys without an icon keep today's Candy text-only tile (Plan 4). Icons come from a fixed set compiled into the firmware and are chosen per key by name in the preset or on the portal.

Out of scope: colour icons, user-uploaded icons, Mac helper changes, any change to tile colours, band, press animation, top bar or strip.

## 2. Icon set — `lib/deck_core/src/icons.h` (pure C++17, no Arduino.h)

- 20 icons, ids 1..20 (0 = no icon). Names, lowercase, ≤ 10 chars:
  `camera globe envelope plane cube gear question play eye-off chat weather printer mic music folder terminal lock moon bolt home`.
- Art is stored as ASCII: per icon 24 strings of exactly 24 chars, `#` = ink, `.` = empty. Reviewable in a diff; editing an icon = editing text.
- Flash cost about 20 × 576 B ≈ 11.5 KB (app slot 3 MB, 39.0 % used at 9369432).
- API:
  - `constexpr uint8_t kIconCount = 20; constexpr uint8_t kIconSize = 24;`
  - `uint8_t iconByName(const char *name)` — case-insensitive exact match, returns 1..20; empty, null or unknown → 0.
  - `const char *iconName(uint8_t id)` — name for 1..20, `""` for 0 or out of range.
  - `struct IconRun { uint8_t x, len; };` and `uint8_t iconRuns(uint8_t id, uint8_t row, IconRun *out, uint8_t cap)` — horizontal runs of `#` in one row, left to right; returns run count (≤ cap); 0 for id 0, out-of-range id or row ≥ 24.
- Art guidelines: 2 px strokes, about 2 px margin inside the 24×24 box, shapes centred, readable at arm's length on a 2" 320×240 panel.

## 3. Key map — `lib/deck_core/src/keymap.h/.cpp`

- `KeyEntry` gains `uint8_t icon;` (0 = none). Default map: all 0.
- `KeyMapError KeyMap::setIcon(uint8_t i, const char *name)`: empty → 0 / None; known name → id / None; unknown → new error `KeyMapError::BadIcon`; bad index → BadIndex.
- NVS blob version 3: `'SDKM', 3, 12 × [type, vlen, value, llen, label, icon], CRC32 LE`. `kKeyMapBlobMax` grows by 12. Versions 1 and 2 still load with icon 0. Load re-validates icon ids (> kIconCount → reject the blob, same as other corrupt fields).
- `operator==` compares icons.

## 4. Portal — `src/wifi_portal.cpp`

- `kFields` 3 → 4: per key type, value, label, icon. Field id `i<n>`, label "Key n icon", length 10, value = `iconName(entry.icon)`.
- `portalTakeStaged`: a failing `setIcon` marks that key bad (existing "Invalid key n" banner).
- Preset loader JS: accepts optional `"icon"` (string ≤ 10 chars, else "key n: bad icon") and fills `i<n>` with it or `""`. The deck still validates the name on Save.
- The param page note lists the valid icon names in one line.

## 5. Preset — `presets/mac-daily.json`

| Key | Label | Icon |
|---|---|---|
| 1 | Screenshot | camera |
| 2 | Chrome | globe |
| 3 | Gmail | envelope |
| 4 | Telegram | plane |
| 5 | Fusion 360 | cube |
| 6 | Settings | gear |
| 7 | Help | question |
| 8 | YouTube | play |
| 9 | Hide app | eye-off |
| 10 | iMessage | chat |
| 11 | Weather | weather |
| 12 | Bambu | printer |

Types and values stay as they are now.

## 6. Tile drawing — `src/display.cpp`

Tile geometry is unchanged from Plan 4: x = c*80+3, y = 24+r*54, 74×50, radius 8, 5 px band.

- Icon tile (entry.icon ≠ 0):
  - Icon box at (x+25, y+8), 24×24. Each run is drawn with `drawFastHLine(x+25+run.x, y+8+row, run.len, look.text)`. Colour = `tileLook().text`: white idle, black from half fade on.
  - Label: one line, baseline y+44, centred in 74 px, max 72 px. Pure helper `chooseOneLine` (next to `chooseLegend` in theme.h) draws `FreeSans7pt7b` (regular only), cut with `fitLine` "..." if wider than 72 px. Bold 7pt dropped (revision 2): FreeType mono rendering at 7 pt / DPI 141 loses the dot of bold 'i'/'j' ("Gmall"), and mixed weights looked uneven. Label text = `displayLabel()` (empty label → derived from value, as today).
  - The 7pt fonts are generated (Adafruit fontconvert, DPI 141, GNU FreeFont 20120503 `FreeSans.ttf` / `FreeSansBold.ttf`; glyph metrics of the 9pt regeneration match the shipped Adafruit header) and committed under `include/fonts/`, with `tools/make_label_fonts.sh` to rebuild them. Cap height 9 px; widest preset label "Screenshot" = 69 px regular. Revision 2026-11-08: the first version (9pt, 68 px, y+45) cut 6 of 12 preset labels, see the Task 3 report.
- Text tile (icon 0): today's `chooseLegend` path, unchanged.
- Repaint scope unchanged: a press repaints only its tile.

## 7. Testing (TDD)

- `test/native/test_icons` (new suite):
  - every icon has 24 rows of 24 chars, only `#`/`.`, at least one ink pixel, and a blank 1 px border;
  - names unique, ≤ 10 chars;
  - `iconByName` exact match, case-insensitive, unknown / empty / null → 0;
  - `iconName` round-trips;
  - `iconRuns` on a hand-checked row, an empty row, and the id/row bounds.
- `test_keymap`: `setIcon` cases; v3 round-trip with icons; v2 blob loads with icon 0; blob with icon id 21 rejected; `==` sees icon changes.
- `test_theme`: `chooseOneLine` bold / regular / cut branches with the fake glyph widths.
- Sim (`test_sim_display`):
  - icon tile: every recorded hline lies inside its 24×24 box, and the label is printed at baseline y+45;
  - pressed icon key draws hlines in black;
  - text tile unchanged (existing tests stay green).
  - Simulator support: `SimLcd` records and rasterizes `drawFastHLine`.
- `test_sim_shot`: the screenshot scenario loads the updated preset with icons. The PNG copy goes to `docs/superpowers/specs/assets/2026-11-08-icons-sim.png` for owner review before flashing.
- Done gate: `pio test -e native`, `pio test -e sim`, Lua tests, and `pio run -e saucedeck -e bringup` all green with zero new warnings.

## 8. Risks

- Icon draw time: about 300 ink pixels as about 60 hline calls per tile, estimated about 1 ms at SPI 20 MHz [unverified]. Fine for single-tile press repaints; a full 12-tile repaint happens only at boot or key map change.
- Recognisability of hand-drawn 24×24 art. Mitigated by the sim screenshot review before flashing, and art is cheap to edit.
- After flashing, the owner must reload the preset on the portal to get icons (stored v2 maps load with icon 0 and keep text tiles).
