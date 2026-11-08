#include <unity.h>
#include <string.h>
#include "icons.h"

void setUp() {}
void tearDown() {}

static const char *const kNames[kIconCount] = {
    "camera", "globe",   "envelope", "plane",    "cube", "gear", "question",
    "play",   "eye-off", "chat",     "weather",  "printer", "mic", "music",
    "folder", "terminal", "lock",    "moon",     "bolt", "home"};

// --- art shape ---

void test_constants() {
  TEST_ASSERT_EQUAL_UINT8(20, kIconCount);
  TEST_ASSERT_EQUAL_UINT8(24, kIconSize);
}

void test_every_icon_is_24x24_hash_dot_with_blank_border() {
  for (uint8_t id = 1; id <= kIconCount; ++id) {
    const char *const *art = iconArt(id);
    TEST_ASSERT_NOT_NULL_MESSAGE(art, iconName(id));
    int ink = 0;
    for (uint8_t r = 0; r < kIconSize; ++r) {
      const char *row = art[r];
      TEST_ASSERT_NOT_NULL_MESSAGE(row, iconName(id));
      TEST_ASSERT_EQUAL_size_t_MESSAGE(24, strlen(row), iconName(id));
      for (uint8_t c = 0; c < kIconSize; ++c) {
        TEST_ASSERT_TRUE_MESSAGE(row[c] == '#' || row[c] == '.', iconName(id));
        if (row[c] == '#') ++ink;
        if (r == 0 || r == 23 || c == 0 || c == 23) TEST_ASSERT_EQUAL_CHAR_MESSAGE('.', row[c], iconName(id));
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(ink > 0, iconName(id));
  }
}

void test_art_out_of_range_is_null() {
  TEST_ASSERT_NULL(iconArt(0));
  TEST_ASSERT_NULL(iconArt(21));
}

// --- names ---

void test_names_in_order() {
  for (uint8_t id = 1; id <= kIconCount; ++id) TEST_ASSERT_EQUAL_STRING(kNames[id - 1], iconName(id));
}

void test_names_unique_short_lowercase() {
  for (uint8_t a = 1; a <= kIconCount; ++a) {
    const char *n = iconName(a);
    TEST_ASSERT_TRUE(strlen(n) >= 1 && strlen(n) <= 10);
    for (const char *p = n; *p; ++p) TEST_ASSERT_FALSE(*p >= 'A' && *p <= 'Z');
    for (uint8_t b = a + 1; b <= kIconCount; ++b) TEST_ASSERT_TRUE(strcmp(n, iconName(b)) != 0);
  }
}

void test_name_out_of_range_is_empty() {
  TEST_ASSERT_EQUAL_STRING("", iconName(0));
  TEST_ASSERT_EQUAL_STRING("", iconName(21));
  TEST_ASSERT_EQUAL_STRING("", iconName(255));
}

// --- iconByName ---

void test_by_name_exact_case_insensitive() {
  TEST_ASSERT_EQUAL_UINT8(1, iconByName("camera"));
  TEST_ASSERT_EQUAL_UINT8(1, iconByName("CAMERA"));
  TEST_ASSERT_EQUAL_UINT8(9, iconByName("Eye-Off"));
  TEST_ASSERT_EQUAL_UINT8(20, iconByName("home"));
}

void test_by_name_rejects() {
  TEST_ASSERT_EQUAL_UINT8(0, iconByName("came"));
  TEST_ASSERT_EQUAL_UINT8(0, iconByName(""));
  TEST_ASSERT_EQUAL_UINT8(0, iconByName(nullptr));
  TEST_ASSERT_EQUAL_UINT8(0, iconByName("camera "));
  TEST_ASSERT_EQUAL_UINT8(0, iconByName("cameras"));
}

void test_by_name_round_trips() {
  for (uint8_t id = 1; id <= kIconCount; ++id) TEST_ASSERT_EQUAL_UINT8(id, iconByName(iconName(id)));
}

// --- iconRuns ---

void test_runs_rebuild_every_row() {
  IconRun runs[24];
  for (uint8_t id = 1; id <= kIconCount; ++id) {
    for (uint8_t r = 0; r < kIconSize; ++r) {
      char rebuilt[25];
      memset(rebuilt, '.', 24);
      rebuilt[24] = '\0';
      uint8_t n = iconRuns(id, r, runs, 24);
      uint8_t lastEnd = 0;
      for (uint8_t i = 0; i < n; ++i) {
        TEST_ASSERT_TRUE(runs[i].len > 0);
        TEST_ASSERT_TRUE(i == 0 || runs[i].x > lastEnd);  // left to right, gap between runs
        TEST_ASSERT_TRUE(runs[i].x + runs[i].len <= 24);
        memset(rebuilt + runs[i].x, '#', runs[i].len);
        lastEnd = static_cast<uint8_t>(runs[i].x + runs[i].len);
      }
      TEST_ASSERT_EQUAL_STRING_MESSAGE(iconArt(id)[r], rebuilt, iconName(id));
    }
  }
}

void test_runs_bounds() {
  IconRun runs[24];
  TEST_ASSERT_EQUAL_UINT8(0, iconRuns(0, 5, runs, 24));
  TEST_ASSERT_EQUAL_UINT8(0, iconRuns(21, 5, runs, 24));
  TEST_ASSERT_EQUAL_UINT8(0, iconRuns(1, 24, runs, 24));
  TEST_ASSERT_EQUAL_UINT8(0, iconRuns(1, 0, runs, 24));  // blank border row
}

void test_runs_cap() {
  // Find a row with at least 2 runs, then ask for 1.
  IconRun all[24];
  for (uint8_t id = 1; id <= kIconCount; ++id) {
    for (uint8_t r = 0; r < kIconSize; ++r) {
      uint8_t n = iconRuns(id, r, all, 24);
      if (n < 2) continue;
      IconRun one[2] = {{99, 99}, {99, 99}};
      TEST_ASSERT_EQUAL_UINT8(1, iconRuns(id, r, one, 1));
      TEST_ASSERT_EQUAL_UINT8(all[0].x, one[0].x);
      TEST_ASSERT_EQUAL_UINT8(all[0].len, one[0].len);
      TEST_ASSERT_EQUAL_UINT8(99, one[1].x);  // untouched
      TEST_ASSERT_EQUAL_UINT8(0, iconRuns(id, r, one, 0));
      return;
    }
  }
  TEST_FAIL_MESSAGE("no row with 2+ runs");
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_constants);
  RUN_TEST(test_every_icon_is_24x24_hash_dot_with_blank_border);
  RUN_TEST(test_art_out_of_range_is_null);
  RUN_TEST(test_names_in_order);
  RUN_TEST(test_names_unique_short_lowercase);
  RUN_TEST(test_name_out_of_range_is_empty);
  RUN_TEST(test_by_name_exact_case_insensitive);
  RUN_TEST(test_by_name_rejects);
  RUN_TEST(test_by_name_round_trips);
  RUN_TEST(test_runs_rebuild_every_row);
  RUN_TEST(test_runs_bounds);
  RUN_TEST(test_runs_cap);
  return UNITY_END();
}
