#include <unity.h>
#include <string.h>
#include "theme.h"

void setUp() {}
void tearDown() {}

// Fake font: bold 9 px/char, regular 8 px/char, space 4 px in both.
static uint8_t boldW(char c) { return c == ' ' ? 4 : 9; }
static uint8_t regW(char c) { return c == ' ' ? 4 : 8; }

static Legend pick(const char *text, uint16_t maxPx = 68) {
  Legend l;
  chooseLegend(text, maxPx, boldW, regW, l);
  return l;
}

// --- rgb565 / constants ---

void test_rgb565_primaries() {
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, rgb565(255, 255, 255));
  TEST_ASSERT_EQUAL_HEX16(0x0000, rgb565(0, 0, 0));
  TEST_ASSERT_EQUAL_HEX16(0xF800, rgb565(255, 0, 0));
  TEST_ASSERT_EQUAL_HEX16(0x07E0, rgb565(0, 255, 0));
  TEST_ASSERT_EQUAL_HEX16(0x001F, rgb565(0, 0, 255));
}

void test_constants_match_spec() {
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x0B, 0x0B, 0x1A), kThemeBg);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0x4D, 0x6D), kKeyHues[0]);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x00, 0xC2, 0xA8), kKeyHues[11]);
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0x9F, 0x0A), kThemeFailed);
  TEST_ASSERT_TRUE(kThemeFailed != kKeyHues[4]);
}

// --- tileLook ---

void test_tile_idle() {
  TileLook t = tileLook(3, 0, false);
  TEST_ASSERT_EQUAL_HEX16(blend565(kThemeBg, kKeyHues[3], 140, 255), t.fill);
  TEST_ASSERT_EQUAL_HEX16(kKeyHues[3], t.band);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, t.text);
}

void test_tile_full_is_accent_black_text() {
  TileLook t = tileLook(6, TileAnim::kFull, false);
  TEST_ASSERT_EQUAL_HEX16(kKeyHues[6], t.fill);
  TEST_ASSERT_EQUAL_HEX16(0x0000, t.text);
}

void test_tile_half_flips_text() {
  TileLook below = tileLook(0, 1, false);
  TileLook half = tileLook(0, 2, false);
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, below.text);
  TEST_ASSERT_EQUAL_HEX16(0x0000, half.text);
  uint16_t idle = blend565(kThemeBg, kKeyHues[0], 140, 255);
  TEST_ASSERT_EQUAL_HEX16(blend565(idle, kKeyHues[0], 2, TileAnim::kFull), half.fill);
}

void test_tile_failed_uses_amber() {
  TileLook t = tileLook(4, TileAnim::kFull, true);
  TEST_ASSERT_EQUAL_HEX16(kThemeFailed, t.band);
  TEST_ASSERT_EQUAL_HEX16(kThemeFailed, t.fill);
  TileLook idle = tileLook(4, 0, true);
  TEST_ASSERT_EQUAL_HEX16(blend565(kThemeBg, kThemeFailed, 140, 255), idle.fill);
}

// --- chooseLegend ---

void test_legend_empty_is_bold_empty() {
  Legend l = pick("");
  TEST_ASSERT_TRUE(l.font == LegendFont::Bold);
  TEST_ASSERT_EQUAL_STRING("", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("", l.lines.line2);
}

void test_legend_short_one_line_bold() {
  Legend l = pick("Mail");  // 36 px
  TEST_ASSERT_TRUE(l.font == LegendFont::Bold);
  TEST_ASSERT_EQUAL_STRING("Mail", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("", l.lines.line2);
}

void test_legend_bold_exact_fit() {
  Legend l = pick("Abcdefg");  // 63 px bold
  TEST_ASSERT_TRUE(l.font == LegendFont::Bold);
  TEST_ASSERT_EQUAL_STRING("", l.lines.line2);
}

void test_legend_one_line_regular_when_bold_too_wide() {
  Legend l = pick("Abcdefgh");  // bold 72, regular 64
  TEST_ASSERT_TRUE(l.font == LegendFont::Regular);
  TEST_ASSERT_EQUAL_STRING("Abcdefgh", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("", l.lines.line2);
}

void test_legend_space_split_bold() {
  Legend l = pick("Gmail in Chrome");  // too wide on one line
  TEST_ASSERT_TRUE(l.font == LegendFont::Bold);
  TEST_ASSERT_EQUAL_STRING("Gmail in", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("Chrome", l.lines.line2);
}

void test_legend_space_split_regular_when_bold_cannot() {
  // Bold: "Abcdefgh" 72 > 68 -> no bold split. Regular: 64 and 8 fit.
  Legend l = pick("Abcdefgh i");
  TEST_ASSERT_TRUE(l.font == LegendFont::Regular);
  TEST_ASSERT_EQUAL_STRING("Abcdefgh", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("i", l.lines.line2);
}

void test_legend_single_word_balanced_split() {
  Legend l = pick("Telegram");  // bold 72, regular 64 -> would fit regular one line
  TEST_ASSERT_TRUE(l.font == LegendFont::Regular);
  l = pick("Telegram", 50);  // bold 72/regular 64 too wide; halves fit bold (36)
  TEST_ASSERT_TRUE(l.font == LegendFont::Bold);
  TEST_ASSERT_EQUAL_STRING("Tele", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("gram", l.lines.line2);
}

void test_legend_single_word_split_regular_when_bold_halves_too_wide() {
  // 11 chars: halves 5/6 -> bold 54 > 50, regular 48 fits.
  Legend l = pick("Abcdefghijk", 50);
  TEST_ASSERT_TRUE(l.font == LegendFont::Regular);
  TEST_ASSERT_EQUAL_STRING("Abcde", l.lines.line1);
  TEST_ASSERT_EQUAL_STRING("fghijk", l.lines.line2);
}

void test_legend_overlong_ellipsis() {
  Legend l = pick("Abcdefghijklmnopqrstuvwxyz", 68);
  TEST_ASSERT_TRUE(l.font == LegendFont::Regular);
  TEST_ASSERT_TRUE(strlen(l.lines.line2) >= 3);
  TEST_ASSERT_EQUAL_STRING("...", l.lines.line2 + strlen(l.lines.line2) - 3);
}

void test_legend_multiword_overlong_falls_back_to_wrap() {
  Legend l = pick("Open the big calendar app now", 68);
  TEST_ASSERT_TRUE(l.font == LegendFont::Regular);
  TEST_ASSERT_EQUAL_STRING("...", l.lines.line2 + strlen(l.lines.line2) - 3);
}

// --- volume segments ---

void test_lit_segments() {
  TEST_ASSERT_EQUAL_UINT8(0, litSegments(0));
  TEST_ASSERT_EQUAL_UINT8(0, litSegments(2));
  TEST_ASSERT_EQUAL_UINT8(1, litSegments(3));
  TEST_ASSERT_EQUAL_UINT8(10, litSegments(50));
  TEST_ASSERT_EQUAL_UINT8(19, litSegments(97));
  TEST_ASSERT_EQUAL_UINT8(20, litSegments(100));
  TEST_ASSERT_EQUAL_UINT8(20, litSegments(250));
}

void test_lit_segments_one_and_two_percent() {
  TEST_ASSERT_EQUAL_UINT8(0, litSegments(1));
  TEST_ASSERT_EQUAL_UINT8(0, litSegments(2));
}

void test_segment_gradient() {
  TEST_ASSERT_EQUAL_HEX16(rgb565(0x30, 0xD1, 0x58), segmentColor(0, false));
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0x45, 0x3A), segmentColor(19, false));
  TEST_ASSERT_EQUAL_HEX16(rgb565(0xFF, 0xD6, 0x0A), segmentColor(10, false));
  TEST_ASSERT_TRUE(segmentColor(5, false) != segmentColor(0, false));
}

void test_segment_muted_grey() {
  TEST_ASSERT_EQUAL_HEX16(kThemeMutedSeg, segmentColor(0, true));
  TEST_ASSERT_EQUAL_HEX16(kThemeMutedSeg, segmentColor(19, true));
}

void test_seg_delta() {
  SegSpan up = segDelta(10, 13);
  TEST_ASSERT_EQUAL_UINT8(10, up.first);
  TEST_ASSERT_EQUAL_UINT8(3, up.count);
  TEST_ASSERT_TRUE(up.lit);
  SegSpan down = segDelta(13, 10);
  TEST_ASSERT_EQUAL_UINT8(10, down.first);
  TEST_ASSERT_EQUAL_UINT8(3, down.count);
  TEST_ASSERT_FALSE(down.lit);
  SegSpan same = segDelta(7, 7);
  TEST_ASSERT_EQUAL_UINT8(0, same.count);
}

// --- card dots ---

void test_card_dot_x() {
  TEST_ASSERT_EQUAL_UINT8(313, cardDotX(0, 1));
  TEST_ASSERT_EQUAL_UINT8(259, cardDotX(0, 7));
  TEST_ASSERT_EQUAL_UINT8(313, cardDotX(6, 7));
  TEST_ASSERT_EQUAL_UINT8(9, cardDotX(1, 7) - cardDotX(0, 7));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_rgb565_primaries);
  RUN_TEST(test_constants_match_spec);
  RUN_TEST(test_tile_idle);
  RUN_TEST(test_tile_full_is_accent_black_text);
  RUN_TEST(test_tile_half_flips_text);
  RUN_TEST(test_tile_failed_uses_amber);
  RUN_TEST(test_legend_empty_is_bold_empty);
  RUN_TEST(test_legend_short_one_line_bold);
  RUN_TEST(test_legend_bold_exact_fit);
  RUN_TEST(test_legend_one_line_regular_when_bold_too_wide);
  RUN_TEST(test_legend_space_split_bold);
  RUN_TEST(test_legend_space_split_regular_when_bold_cannot);
  RUN_TEST(test_legend_single_word_balanced_split);
  RUN_TEST(test_legend_single_word_split_regular_when_bold_halves_too_wide);
  RUN_TEST(test_legend_overlong_ellipsis);
  RUN_TEST(test_legend_multiword_overlong_falls_back_to_wrap);
  RUN_TEST(test_lit_segments);
  RUN_TEST(test_lit_segments_one_and_two_percent);
  RUN_TEST(test_segment_gradient);
  RUN_TEST(test_segment_muted_grey);
  RUN_TEST(test_seg_delta);
  RUN_TEST(test_card_dot_x);
  return UNITY_END();
}
