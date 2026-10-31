// Sim LCD framebuffer: rasterizer fidelity (Adafruit GFX algorithms), real fonts, BMP output,
// and a screenshot of the real src/display.cpp for visual review (.pio/shots/deck.bmp).
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unity.h>
#include <vector>
#include "Adafruit_ST7789.h"
#include "Fonts/FreeSans9pt7b.h"
#include "frame.h"
#include "sim_deck.h"
#include "sim_test.h"

void setUp() {}
void tearDown() {}

static constexpr uint16_t kRed = 0xF800, kWhite = 0xFFFF, kBlack = 0;

static Adafruit_ST7789 &tft() {
  static Adafruit_ST7789 t(nullptr, 0, 0, 0);
  return t;
}
static uint16_t px(int x, int y) { return board().lcd.fb[y][x]; }

SIM_TEST(test_fill_rect_sets_pixels_and_clips) {
  tft().fillScreen(kBlack);
  tft().fillRect(10, 20, 5, 3, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, px(10, 20));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(14, 22));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(15, 22));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(14, 23));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(9, 20));
  tft().fillRect(-5, -5, 10, 10, kWhite);  // clipped to 0..4
  TEST_ASSERT_EQUAL_HEX16(kWhite, px(0, 0));
  TEST_ASSERT_EQUAL_HEX16(kWhite, px(4, 4));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(5, 5));
  tft().fillRect(315, 235, 20, 20, kRed);  // clipped at the far corner, no overrun
  TEST_ASSERT_EQUAL_HEX16(kRed, px(319, 239));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(315, 235));
}

SIM_TEST(test_fill_screen_fills_all) {
  tft().fillScreen(kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, px(0, 0));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(319, 239));
}

SIM_TEST(test_round_rect_corner_stays_background) {
  tft().fillScreen(kBlack);
  tft().fillRoundRect(10, 10, 40, 30, 8, kRed);
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(10, 10));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(49, 10));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(10, 39));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(30, 25));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(10, 25));  // left edge, mid height
  TEST_ASSERT_EQUAL_HEX16(kRed, px(30, 10));  // top edge, mid width
}

SIM_TEST(test_fill_circle_centre_and_edge) {
  tft().fillScreen(kBlack);
  tft().fillCircle(100, 100, 10, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, px(100, 100));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(110, 100));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(90, 100));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(100, 90));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(100, 110));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(111, 100));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(108, 108));
}

SIM_TEST(test_line_triangle_rect_outline) {
  tft().fillScreen(kBlack);
  tft().drawLine(0, 0, 9, 9, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, px(0, 0));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(5, 5));
  TEST_ASSERT_EQUAL_HEX16(kRed, px(9, 9));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(5, 4));
  tft().drawRect(20, 20, 6, 4, kWhite);
  TEST_ASSERT_EQUAL_HEX16(kWhite, px(20, 20));
  TEST_ASSERT_EQUAL_HEX16(kWhite, px(25, 23));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(22, 21));
  tft().fillTriangle(50, 50, 60, 50, 50, 60, kRed);
  TEST_ASSERT_EQUAL_HEX16(kRed, px(51, 51));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(59, 59));
}

SIM_TEST(test_circle_helper_draws_quarter_arc) {
  tft().fillScreen(kBlack);
  tft().drawCircleHelper(100, 100, 10, 4, kRed);  // bottom-right quarter
  TEST_ASSERT_EQUAL_HEX16(kRed, px(107, 107));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(93, 93));
}

SIM_TEST(test_custom_font_print_sets_glyph_pixels_and_advances) {
  tft().fillScreen(kBlack);
  tft().setFont(&FreeSans9pt7b);
  tft().setTextSize(1);
  tft().setTextColor(kWhite, kWhite);
  tft().setCursor(20, 40);
  tft().print("H");
  const GFXglyph &g = FreeSans9pt7b.glyph['H' - FreeSans9pt7b.first];
  TEST_ASSERT_EQUAL(20 + g.xAdvance, tft().getCursorX());
  TEST_ASSERT_EQUAL(40, tft().getCursorY());  // y = baseline, unchanged
  const int x0 = 20 + g.xOffset, y0 = 40 + g.yOffset;
  int inside = 0, outside = 0;
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++)
      if (px(x, y) != kBlack) {
        if (x >= x0 && x < x0 + g.width && y >= y0 && y < y0 + g.height) inside++;
        else outside++;
      }
  TEST_ASSERT_GREATER_THAN(20, inside);
  TEST_ASSERT_EQUAL(0, outside);
  // 'H' stems: left column of the glyph box is lit on its first row, middle top is not.
  TEST_ASSERT_EQUAL_HEX16(kWhite, px(x0, y0));
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(x0 + g.width / 2, y0));
  // The real 9pt 'H' is 13 px wide; the old 10 px stand-in is gone.
  TEST_ASSERT_EQUAL(13, g.xAdvance);
}

SIM_TEST(test_classic_font_size2_h_is_10x14) {
  tft().fillScreen(0x1234);
  tft().setFont(nullptr);
  tft().setTextSize(2);
  tft().setTextColor(kWhite, kBlack);
  tft().setCursor(10, 10);
  tft().print("H");
  int minx = 999, maxx = -1, miny = 999, maxy = -1;
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++)
      if (px(x, y) == kWhite) {
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
      }
  TEST_ASSERT_EQUAL(10, minx);
  TEST_ASSERT_EQUAL(19, maxx);
  TEST_ASSERT_EQUAL(10, miny);
  TEST_ASSERT_EQUAL(23, maxy);
  TEST_ASSERT_EQUAL_HEX16(kWhite, px(11, 23));   // 2x2 block of the left stem
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(12, 10));   // opaque bg between stems
  TEST_ASSERT_EQUAL_HEX16(kBlack, px(20, 10));   // opaque spacing column
  TEST_ASSERT_EQUAL_HEX16(0x1234, px(22, 10));   // next cell untouched
  TEST_ASSERT_EQUAL(22, tft().getCursorX());     // 6 px * 2
}

SIM_TEST(test_save_bmp_layout_and_roundtrip) {
  tft().fillScreen(kBlack);
  tft().fillRect(0, 0, 1, 1, kRed);  // (0,0) top-left
  tft().fillRect(319, 239, 1, 1, kWhite);
  const char *path = SIM_PROJECT_DIR "/.pio/shot_test.bmp";
  TEST_ASSERT_TRUE(board().lcd.saveBmp(path));
  FILE *f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL(f);
  std::vector<uint8_t> b(54 + 320 * 240 * 3 + 16);
  const size_t n = fread(b.data(), 1, b.size(), f);
  fclose(f);
  remove(path);
  TEST_ASSERT_EQUAL(54 + 320 * 240 * 3, n);
  TEST_ASSERT_EQUAL('B', b[0]);
  TEST_ASSERT_EQUAL('M', b[1]);
  TEST_ASSERT_EQUAL(54, b[10]);
  TEST_ASSERT_EQUAL(40, b[14]);
  TEST_ASSERT_EQUAL(320 & 0xFF, b[18]);
  TEST_ASSERT_EQUAL(320 >> 8, b[19]);
  TEST_ASSERT_EQUAL(240, b[22]);  // positive height = bottom-up
  TEST_ASSERT_EQUAL(24, b[28]);
  // Pixel (0,0) is the first pixel of the LAST row; stored B,G,R; RGB565 red 0xF800 expands (bit replication) to 0xFF,0,0.
  const size_t last = 54 + 239 * 320 * 3;
  TEST_ASSERT_EQUAL(0x00, b[last + 0]);
  TEST_ASSERT_EQUAL(0x00, b[last + 1]);
  TEST_ASSERT_EQUAL(0xFF, b[last + 2]);
  // Pixel (319,239) white is the last pixel of the FIRST stored row: 5-6-5 expands to 0xFF.
  const size_t first_row_end = 54 + 319 * 3;
  TEST_ASSERT_EQUAL(0xFF, b[first_row_end + 0]);
  TEST_ASSERT_EQUAL(0xFF, b[first_row_end + 1]);
  TEST_ASSERT_EQUAL(0xFF, b[first_row_end + 2]);
}

// --- screenshot of the real firmware ---

static void sendMsg(uint8_t type, const uint8_t *p, uint8_t len) {
  uint8_t m[kReportSize] = {kProtoVersion, type, len};
  memcpy(m + 3, p, len);
  uint8_t f[kFrameSize];
  frameMessage(m, f);
  board().serial.rx.insert(board().serial.rx.end(), f, f + kFrameSize);
}
static void hello() {
  const uint8_t p[1] = {1};
  sendMsg(MSG_HELLO, p, 1);
}
static void runWithHelper(SimDeck &sim, uint32_t ms) {
  while (ms) {
    hello();
    uint32_t step = ms < 1000 ? ms : 1000;
    sim.run(step);
    ms -= step;
  }
}

SIM_TEST(test_screenshot_of_real_display) {
  SimDeck sim;
  TEST_ASSERT_TRUE(sim.loadPresetIntoNvs(SIM_PROJECT_DIR "/presets/mac-daily.json"));
  sim.boot();
  hello();
  const uint8_t vol[2] = {65, 0}, mic[2] = {70, 0};
  sendMsg(MSG_VOLUME, vol, 2);
  sendMsg(MSG_MIC, mic, 2);
  const char *txt = "Daft Punk - One More Time";
  uint8_t p[5 + 48] = {1, 1, 0, 0, static_cast<uint8_t>(strlen(txt))};
  memcpy(p + 5, txt, strlen(txt));
  sendMsg(MSG_CARD, p, static_cast<uint8_t>(5 + strlen(txt)));
  runWithHelper(sim, 1700);  // past the splash
  sim.press(7);
  sim.run(60);  // mid-fade: > 25 ms debounce, fade not finished
  mkdir(SIM_PROJECT_DIR "/.pio", 0755);
  mkdir(SIM_PROJECT_DIR "/.pio/shots", 0755);
  const char *path = SIM_PROJECT_DIR "/.pio/shots/deck.bmp";
  remove(path);
  TEST_ASSERT_TRUE(board().lcd.saveBmp(path));
  struct stat st;
  TEST_ASSERT_EQUAL(0, stat(path, &st));
  TEST_ASSERT_EQUAL(54 + 320 * 240 * 3, st.st_size);
  // Not a blank/black screen: tiles and text put plenty of non-background pixels on it.
  int lit = 0;
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++)
      if (px(x, y) != kBlack) lit++;
  TEST_ASSERT_GREATER_THAN(2000, lit);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_fill_rect_sets_pixels_and_clips);
  RUN_TEST(test_fill_screen_fills_all);
  RUN_TEST(test_round_rect_corner_stays_background);
  RUN_TEST(test_fill_circle_centre_and_edge);
  RUN_TEST(test_line_triangle_rect_outline);
  RUN_TEST(test_circle_helper_draws_quarter_arc);
  RUN_TEST(test_custom_font_print_sets_glyph_pixels_and_advances);
  RUN_TEST(test_classic_font_size2_h_is_10x14);
  RUN_TEST(test_save_bmp_layout_and_roundtrip);
  RUN_TEST(test_screenshot_of_real_display);
  return UNITY_END();
}
