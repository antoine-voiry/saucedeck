# LCD theme "Candy" Implementation Plan — Plan 4

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Replace the grey first-pass LCD look with the "Candy" theme the owner picked (mockup A, 2026-10-31): one vivid colour per key, rounded tiles with gaps, bold labels, segmented gradient volume bar, mic pill, card dots, bold clock.

**Architecture:** Colour/legend/segment decisions are pure functions in `lib/deck_core/src/theme.h` (native TDD). `src/display.cpp` only draws. The device sim gets a real framebuffer + real Adafruit fonts so `pio test -e sim` can write screenshots (BMP) of the real `display.cpp` output for visual review before flashing.

**Reference mockup:** `docs/superpowers/specs/assets/2026-10-31-candy-mockup.png` (640×480 = 2× of 320×240) and its generator `tools/lcd_mock.py` (committed in Task 1; geometry below comes from it).

## Global Constraints

- Panel 320×240 landscape, RGB565. Firmware fonts from Adafruit GFX 1.12.x: `FreeSans9pt7b` (regular), `FreeSansBold9pt7b` (labels), `FreeSansBold12pt7b` (clock, volume %). Custom fonts draw no background: fill first.
- Colours (24-bit, convert with `rgb565(r,g,b) = (r>>3)<<11 | (g>>2)<<5 | (b>>3)`):
  - Background `#0B0B1A`; bars (top bar, strip) `#14142B`; segment off `#34344F`; text white `#FFFFFF`; pressed text black.
  - Key hues by position 1..12: `#FF4D6D #FFB000 #EA4335 #2AABEE #FF7A00 #8E8E93 #34C759 #FF0033 #AF52DE #30D158 #0A84FF #00C2A8`.
  - Failed press accent `#FF9F0A` (amber; distinct from key 5's `#FF7A00`). Helper dot green `#30D158`, Wi-Fi dot blue `#0A84FF`, mic pill live `#30D158`, mic pill muted `#FF453A`, mic unknown `#8E8E93`.
  - Volume gradient across 20 segments: `#30D158` → `#FFD60A` at the middle → `#FF453A` at the end (linear in RGB per half). Muted: lit segments `#8E8E93`.
- Geometry (from the mockup):
  - Top bar y 0..21 (h 22), bg bars colour. Clock `FreeSansBold12pt7b` at cursor (6, 18), white. Slot-0 text `FreeSans9pt7b` at x 76, baseline 16, max width 208 (to x 284), colour `#C7C7E0`. Helper dot circle centre (294, 10) r 4; Wi-Fi dot centre (308, 10) r 4.
  - Tiles: column c, row r: x = c*80 + 3, y = 24 + r*54, w 74, h 50, corner radius 8, background colour shows in the gaps. Top band: same x, y, w 74, h 5, radius 3, colour = accent. Label max width 68 px, centred in the 74 px tile; one line baseline y+32; two lines baselines y+23 and y+41.
  - Strip y 188..239, bars colour. Volume segments: 20, each 6×15, x = 8 + i*8, y 194, radius 2. Volume text `FreeSansBold12pt7b` at (172, 208), white, "MUTED" in `#FF453A` when muted; clear its box (172..222, 190..211) before drawing (custom font, no background).
  - Mic pill: rounded rect x 226..312, y 193..209 (w 87, h 17), radius 8; text `FreeSansBold9pt7b` centred, black; "MIC 70%" / "MIC MUTE" / "MIC LIVE".
  - Line 2 (y 214..239): card icon 18×18 at (8, 216) radius 5 with its letter centred in `FreeSansBold9pt7b` white; card text `FreeSans9pt7b` at x 34 baseline 231, max width 248, white. Card dots: one per card (max 7), 5 px circles at centre x = 292 + k*9 (right-aligned so the last dot ends ≤ 316), y 224; current = white, others segment-off. Banners keep their text, drawn in `FreeSansBold9pt7b` at x 8 baseline 231 (APPLY in `#FFD60A`, others white).
- Repaint budget: a key press repaints only that tile; a volume step repaints only the segments that changed plus the % text; card rotation repaints line 2 only (existing dirty flags/gens stay).
- TDD for all `deck_core` logic; `src/display.cpp` stays drawing-only.
- Done gate per task: `~/.platformio/penv/bin/pio test -e native` green, `~/.platformio/penv/bin/pio test -e sim` green, `~/.platformio/penv/bin/pio run -e saucedeck -e bringup` SUCCESS with zero new warnings.

---

### Task 1: Sim framebuffer, real fonts, BMP screenshots (+ mockup asset)

**Files:** `test/sim/lib/simdeck/src/sim/lcd.h`, `test/sim/lib/simdeck/src/fakes.cpp`, `test/sim/lib/simdeck/src/Fonts/*` (real Adafruit font headers for the three fonts above; BSD licence note in a comment), a new sim suite `test/native/test_sim_shot/` (or the repo's sim suite dir convention — check `platformio.ini [env:sim] test_filter`), `tools/lcd_mock.py` (copy of the controller's mockup generator at `/private/tmp/claude-501/-Users-antoine-saucedeck/3b77219d-3076-4732-b603-147a582eab17/scratchpad/mock/mock.py`, made repo-relative: font dir = `.pio/libdeps/saucedeck/Adafruit GFX Library/Fonts/`, output dir argument), `docs/superpowers/specs/assets/2026-10-31-candy-mockup.png` (copy of `.../scratchpad/mock/A_candy.png`).

- SimLcd gains `uint16_t fb[240][320]` (rotation-3 landscape coordinates as drawn) and `bool saveBmp(const char *path) const` (24-bit BMP, RGB565 expanded). Every fake primitive (`fillScreen`, `fillRect`, `drawRect`, `fillRoundRect`, `fillCircle`, `fillTriangle`, `drawLine`, `drawCircleHelper`, `print`) rasterizes into `fb` with clipping, using the Adafruit GFX algorithms (classic 5×7 `glcdfont` for `setFont(nullptr)` at `setTextSize(n)`, opaque background when bg != fg; custom GFX fonts: glyph bitmaps, cursor y = baseline, transparent). Existing recording (`texts`, `rects`) stays.
- Replace the 10 px stand-in `FreeSans9pt7b` with the real header. Fix any sim test that depended on the 10 px advance (keep each test's intent; note each change in the report).
- Tests first (RED → GREEN): fillRect pixels and clipping; fillRoundRect corner pixel stays background; fillCircle centre/edge; custom-font `print` sets pixels inside the glyph box and advances the cursor by `xAdvance`; classic font size 2 draws a 10×14 'H' footprint; `saveBmp` writes a 54-byte header + 320×240×3 rows (bottom-up, 4-byte aligned) and the pixel at (0,0) round-trips.
- Screenshot scenario: boot the sim deck with the `presets/mac-daily.json` labels (the sim preset already used by sim tests), press key 7 mid-fade, set volume 65 %, mic 70 %, send a card; write `.pio/shots/deck.bmp`. The test asserts the file exists and a few pixels are not background (it is a review artifact, not a golden image).
- Commit: "Sim: RGB565 framebuffer, real GFX fonts, BMP screenshots" + `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

### Task 2: `theme.h` — pure colour, legend, segment logic

**Files:** `lib/deck_core/src/theme.h`, `test/native/test_theme/test_main.cpp` (new suite; follow existing suite layout).

**Produces:**
- `constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)`; colour constants `kThemeBg, kThemeBar, kThemeSegOff, kThemeFailed, kThemeHelper, kThemeWifi, kThemeMicLive, kThemeMicMuted, kThemeMicUnknown, kThemeMutedSeg, kThemeTopText` and `kKeyHues[kKeyCount]` (values in Global Constraints).
- `struct TileLook { uint16_t fill, band, text; }; TileLook tileLook(uint8_t key, uint8_t level, bool failed)` — accent = failed ? kThemeFailed : kKeyHues[key]; idle fill = `blend565(kThemeBg, accent, 140, 255)`; fill = `blend565(idleFill, accent, level, TileAnim::kFull)`; band = accent; text = white, black when `level * 2 >= TileAnim::kFull`. (Uses `blend565` from tile_anim.h.)
- `enum class LegendFont : uint8_t { Bold, Regular }; struct Legend { LegendFont font; LabelLines lines; }; void chooseLegend(const char *text, uint16_t maxPx, GlyphWidthFn bold, GlyphWidthFn regular, Legend &out)` — order: (1) one line bold if it fits; (2) one line regular; (3) two lines split at a space, bold if both lines fit bold, else regular if both fit regular; (4) a single word too wide: split into two halves at the index minimising the wider half (no hyphen), bold if both fit, else regular; (5) otherwise `wrapLabel` with regular (existing cut + "..."). Empty text → one empty line, Bold.
- `constexpr uint8_t kVolSegments = 20; uint8_t litSegments(uint8_t pct)` = round(pct * 20 / 100), clamped; `uint16_t segmentColor(uint8_t i, bool muted)` (gradient / muted grey); `struct SegSpan { uint8_t first, count; bool lit; }; SegSpan segDelta(uint8_t oldLit, uint8_t newLit)`.
- `int16_t cardDotX(uint8_t k, uint8_t count)` centre x = 313 - (count-1-k)*9 (right aligned, last dot ends ≤ 316; corrected from uint8_t, which wrapped).

**Tests first** with a fake glyph-width table (e.g. bold = 9 px/char, regular = 8 px/char, space = 4) covering every `chooseLegend` branch (incl. "Telegram"-like word → balanced split like "Tele"/"gram", "Gmail in Chrome" → space split, overlong → "..."), `tileLook` idle/full/half/failed/text flip, gradient endpoints and middle (`segmentColor(0)` = green 565, `segmentColor(19)` = red 565, `segmentColor(10)` within one step of yellow), `litSegments` 0/1/2/50/97/100, `segDelta` up/down/no change, `cardDotX` for 1 and 7 cards.

Commit: "Theme: pure tile colours, legend font choice, volume segments" + Co-Authored-By line.

### Task 3: `display.cpp` Candy redraw + sim scenarios + screenshot review

**Files:** `src/display.cpp`, `test/native/test_sim_display/*` (update expectations), `docs/superpowers/specs/2026-10-31-lcd-legends-status-design.md` (§4 layout: note Plan 4 theme + link to plan), `docs/kb/toolchain.md` (flash size line after build).

- Redraw everything per Global Constraints using `theme.h`. Keep the existing ScreenModel dirty flags/gens; volume delta uses `segDelta`; top bar/line 2/tiles repaint scopes unchanged. Splash: bg colour, "SAUCEDECK" in `FreeSansBold18pt7b`-sized look is optional — keep it simple: `FreeSansBold12pt7b` centred in white with a hue-coloured underline bar.
- Sim tests first: update/extend `test_sim_display` for the new geometry (tile fill rect/round-rect at x = c*80+3; key 7 press repaints only tile 7 region; volume step 50 → 55 % repaints only the changed segments (+ text box); card dots count; mic pill text). RED against the old display, then GREEN.
- Generate `.pio/shots/deck.bmp` (Task 1 scenario) and copy it to `docs/superpowers/specs/assets/2026-10-31-candy-sim.png` (convert BMP → PNG with `sips -s format png`). Compare against the mockup; list differences in the report.
- Done gate + flash size note in kb. Do not upload.
- Commit: "LCD: Candy theme (per-key colours, rounded tiles, gradient volume, mic pill, card dots)" + "Not bench-verified." + Co-Authored-By line.

### Task 4 (controller + operator): flash and look

Upload (`--upload-port` explicit if auto fails; helper releases port via pre-upload hook), owner checks legibility at arm's length, press feedback, volume bar, card dots. Note: the deck shows labels only after the preset is (re)loaded through the portal if the stored key map has none.
