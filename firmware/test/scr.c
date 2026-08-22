#include <stdio.h>
#include <stddef.h>

#include <unity.h>

#include "scr_trans.inc"

#define NUM_LANGS (sizeof(scr_trans_map) / sizeof(scr_trans_t))
#define NUM_STRINGS (sizeof(scr_trans_t) / sizeof(const char*))

static void test_translations_complete() {
  // ensure the translation struct can be iterated as a string array
  _Static_assert(sizeof(scr_trans_t) % sizeof(const char*) == 0, "scr_trans_t must only contain strings");

  // check all strings are either set or unset in all languages
  int failures = 0;
  for (size_t s = 0; s < NUM_STRINGS; s++) {
    int have = 0;
    const char* sample = NULL;
    for (size_t l = 0; l < NUM_LANGS; l++) {
      const char* const* strings = (const char* const*)&scr_trans_map[l];
      if (strings[s] != NULL) {
        have++;
        sample = strings[s];
      }
    }
    if (have != 0 && have != (int)NUM_LANGS) {
      printf("string %zu only set in %d of %zu languages: \"%s\"\n", s, have, NUM_LANGS, sample);
      failures++;
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, failures, "some translations are incomplete (see above)");
}

void suite_scr() {
  RUN_TEST(test_translations_complete);
}
