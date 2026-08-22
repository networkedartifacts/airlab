#include <stdio.h>

#include <unity.h>
#include <lvgl.h>

#include "fnt.h"
#include "stm.h"

#include "stm_data.inc"

const size_t stm_num_entries = sizeof(stm_entries) / sizeof(stm_entry_t);

// mirror of the bubble geometry in lvx.c (lvx_bubble_create/update)
#define BUBBLE_WIDTH 200
#define BUBBLE_LINES 3

static int check_text(size_t index, const char* lang, const char* text) {
  // skip missing texts (reported by test_statements_translated)
  if (text == NULL) {
    return 0;
  }

  // count lines using the same layout call as lvx_bubble_update
  lv_point_t size = {0};
  lv_txt_get_size(&size, text, &fnt_16, 0, 0, BUBBLE_WIDTH, LV_TEXT_FLAG_NONE);
  int lines = size.y / fnt_16.line_height;

  // report overflow
  if (lines > BUBBLE_LINES) {
    printf("statement %zu (%s) needs %d lines: \"%s\"\n", index, lang, lines, text);
    return 1;
  }

  return 0;
}

static void test_statements_fit_bubble() {
  // check all statements in all languages
  int failures = 0;
  for (size_t i = 0; i < sizeof(stm_entries) / sizeof(stm_entry_t); i++) {
    failures += check_text(i, "de", stm_entries[i].text_de);
    failures += check_text(i, "en", stm_entries[i].text_en);
    failures += check_text(i, "es", stm_entries[i].text_es);
    failures += check_text(i, "fr", stm_entries[i].text_fr);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures, "some statements overflow the bubble (see above)");
}

static void test_statements_translated() {
  // check all statements have all translations
  int failures = 0;
  for (size_t i = 0; i < sizeof(stm_entries) / sizeof(stm_entry_t); i++) {
    stm_entry_t* entry = &stm_entries[i];
    const char* texts[] = {entry->text_de, entry->text_en, entry->text_es, entry->text_fr};
    const char* langs[] = {"de", "en", "es", "fr"};
    for (size_t j = 0; j < 4; j++) {
      if (texts[j] == NULL) {
        printf("statement %zu is missing the %s text\n", i, langs[j]);
        failures++;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures, "some statements are missing translations (see above)");
}

void suite_bubbles() {
  RUN_TEST(test_statements_translated);
  RUN_TEST(test_statements_fit_bubble);
}
