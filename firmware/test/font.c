#include <stdio.h>

#include <unity.h>
#include <lvgl.h>

#include "fnt.h"
#include "stm.h"

#include "scr_trans.inc"

extern stm_entry_t stm_entries[];
extern const size_t stm_num_entries;

static const lv_font_t* fonts[] = {&fnt_24, &fnt_16, &fnt_8};
static const char* font_names[] = {"fnt_24", "fnt_16", "fnt_8"};

static int check_string(const char* what, const char* text) {
  // skip missing texts
  if (text == NULL) {
    return 0;
  }

  // verify all code points have glyphs in all fonts
  int failures = 0;
  for (size_t f = 0; f < sizeof(fonts) / sizeof(fonts[0]); f++) {
    uint32_t i = 0;
    for (;;) {
      // get next code point
      uint32_t cp = _lv_txt_encoded_next(text, &i);
      if (cp == 0) {
        break;
      } else if (cp == '\n') {
        continue;
      }

      // check glyph
      lv_font_glyph_dsc_t dsc;
      if (!lv_font_get_glyph_dsc(fonts[f], &dsc, cp, 0)) {
        printf("%s: U+%04X missing in %s: \"%s\"\n", what, (unsigned)cp, font_names[f], text);
        failures++;
      }
    }
  }

  return failures;
}

static void test_statement_glyphs() {
  // check all statements in all languages
  int failures = 0;
  for (size_t i = 0; i < stm_num_entries; i++) {
    failures += check_string("statement", stm_entries[i].text_de);
    failures += check_string("statement", stm_entries[i].text_en);
    failures += check_string("statement", stm_entries[i].text_es);
    failures += check_string("statement", stm_entries[i].text_fr);
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures, "some statements use unavailable glyphs (see above)");
}

static void test_translation_glyphs() {
  // ensure the translation struct can be iterated as a string array
  _Static_assert(sizeof(scr_trans_t) % sizeof(const char*) == 0, "scr_trans_t must only contain strings");

  // check all language names and translations
  int failures = 0;
  for (size_t l = 0; l < sizeof(scr_trans_map) / sizeof(scr_trans_t); l++) {
    failures += check_string("language", scr_lang_str[l]);
    const char* const* strings = (const char* const*)&scr_trans_map[l];
    for (size_t s = 0; s < sizeof(scr_trans_t) / sizeof(const char*); s++) {
      failures += check_string("translation", strings[s]);
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures, "some translations use unavailable glyphs (see above)");
}

void suite_font() {
  RUN_TEST(test_statement_glyphs);
  RUN_TEST(test_translation_glyphs);
}
