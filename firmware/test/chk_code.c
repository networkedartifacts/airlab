#include <math.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "chk_code.h"

// The reference vectors are the exact bytes the page at
// https://airlab.today/ac/ decodes. Agreeing with that page byte for byte is
// the only thing that makes a scanned link readable, so these are checked as
// exact strings rather than as properties: a change here that does not also
// change the page breaks every code this firmware draws.

static const char* VENT_DIGITS =
    "86679433062436804507003507328709065617309143222556475833378691113386767633300976971894703934399501674";

static const char* STOVE_DIGITS =
    "54882454985712252906621117273923262552255326696532099980007044628158116131541422226649803647927664205"
    "67614105122610685249966654251546165914360500839630320354150694991508778115473466487265108213303637760"
    "686618274481904859881791980685498193980672036049948681244672310811169402980";

static void vent_samples(float* out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    double t = i * 5.0 / 3600.0;
    out[i] = (float)floor(425 + (843 - 425) * exp(-2.9 * t) + 0.5);
  }
}

static void test_a_ventilation_payload_matches_the_page() {
  float samples[72];
  vent_samples(samples, 72);

  // ach, achSe, r2, c0, c1, cout, pre
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, samples[71], 425, 6};
  const chk_code_meta_t meta = {.minute = 1000000, .device = 0xAB12, .room = 0, .cadence = 2};

  char digits[CHK_CODE_MAX_DIGITS];
  int step = 0;
  size_t bytes = 0;
  TEST_ASSERT_TRUE(chk_code_pack('A', &meta, fields, 7, samples, 72, 153, digits, sizeof(digits), &step, &bytes));

  TEST_ASSERT_EQUAL_STRING(VENT_DIGITS, digits);
  TEST_ASSERT_EQUAL_INT_MESSAGE(1, step, "a short check keeps the finest step");
  TEST_ASSERT_EQUAL_size_t(42, bytes);
}

static void test_a_stove_payload_matches_the_page() {
  float samples[120];
  for (size_t i = 0; i < 120; i++) {
    samples[i] = (float)(600 + (int)floor(200 * sin(i / 10.0) + 0.5) + (int)i * 3);
  }

  // ce, hoodAch, slope1, slope3, c0, noxPeak, pre, pass1, pass2, pass3
  const float fields[] = {62, 4.1f, 200, 76, 612, 46, 12, 24, 40, 24};
  const chk_code_meta_t meta = {.minute = 1000000, .device = 0xAB12, .room = 0, .cadence = 2};

  char digits[CHK_CODE_MAX_DIGITS];
  int step = 0;
  size_t bytes = 0;
  TEST_ASSERT_TRUE(chk_code_pack('E', &meta, fields, 10, samples, 120, 153, digits, sizeof(digits), &step, &bytes));

  TEST_ASSERT_EQUAL_STRING(STOVE_DIGITS, digits);
  TEST_ASSERT_EQUAL_size_t(115, bytes);
}

static void test_a_long_check_loses_resolution_rather_than_failing() {
  // the encoder walks the steps from fine to coarse and keeps the first that
  // fits, so a check too long for the finest step still produces a link
  float samples[400];
  for (size_t i = 0; i < 400; i++) {
    samples[i] = (float)(500 + (i * 37) % 900);  // deliberately noisy, so deltas are large
  }
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, 500, 425, 6};
  const chk_code_meta_t meta = {.minute = 1000000, .device = 0xAB12, .room = 0, .cadence = 2};

  char digits[CHK_CODE_MAX_DIGITS];
  int step = 0;
  size_t bytes = 0;
  TEST_ASSERT_TRUE(chk_code_pack('A', &meta, fields, 7, samples, 400, 153, digits, sizeof(digits), &step, &bytes));

  TEST_ASSERT_TRUE_MESSAGE(step > 1, "a long noisy check must have been coarsened");
  TEST_ASSERT_TRUE_MESSAGE(bytes <= 153, "and must fit the budget it was given");
}

static void test_an_unknown_letter_is_refused() {
  float samples[4] = {1, 2, 3, 4};
  const float fields[] = {0, 0, 0, 0, 0, 0, 0};
  const chk_code_meta_t meta = {0};
  char digits[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_FALSE(chk_code_pack('Z', &meta, fields, 7, samples, 4, 153, digits, sizeof(digits), NULL, NULL));
}

static void test_a_field_out_of_range_is_refused() {
  // c0 is thirteen bits of ppm, so 9000 fits and 90000 cannot
  float samples[8] = {600, 601, 602, 603, 604, 605, 606, 607};
  const chk_code_meta_t meta = {0};
  char digits[CHK_CODE_MAX_DIGITS];

  const float ok[] = {2.9f, 0.15f, 0.99f, 8000, 600, 425, 6};
  TEST_ASSERT_TRUE(chk_code_pack('A', &meta, ok, 7, samples, 8, 153, digits, sizeof(digits), NULL, NULL));

  const float bad[] = {2.9f, 0.15f, 0.99f, 90000, 600, 425, 6};
  TEST_ASSERT_FALSE_MESSAGE(chk_code_pack('A', &meta, bad, 7, samples, 8, 153, digits, sizeof(digits), NULL, NULL),
                            "a value too large for its field must fail rather than wrap");
}

static void test_the_digits_are_only_digits() {
  // the payload rides in a numeric-mode QR segment, which has no other alphabet
  float samples[72];
  vent_samples(samples, 72);
  const float fields[] = {2.9f, 0.15f, 0.99f, 843, samples[71], 425, 6};
  const chk_code_meta_t meta = {.minute = 1000000, .device = 0xAB12, .room = 0, .cadence = 2};

  char digits[CHK_CODE_MAX_DIGITS];
  TEST_ASSERT_TRUE(chk_code_pack('A', &meta, fields, 7, samples, 72, 153, digits, sizeof(digits), NULL, NULL));

  for (const char* p = digits; *p != '\0'; p++) {
    TEST_ASSERT_TRUE_MESSAGE(*p >= '0' && *p <= '9', "the payload must be all digits");
  }
  // and never a leading zero, which would be lost converting back to bytes
  TEST_ASSERT_NOT_EQUAL('0', digits[0]);
}

// Encodes the link the way the screen does, but with buffers of its own that
// cannot alias, and returns the symbol. The firmware once handed the library
// one array for both scratch and output, which the library forbids: the
// symbol came out with half its modules wrong and every size test still
// passed, so this is checked module by module.
static int reference_symbol(char letter, const char* digits, uint8_t* out) {
  static uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  static uint8_t head_buf[64];
  char head[64];
  snprintf(head, sizeof(head), "%s%c", CHK_CODE_PREFIX, letter);
  memcpy(head_buf, head, strlen(head));

  static uint8_t num_buf[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  struct qrcodegen_Segment segs[2];
  segs[0] = qrcodegen_makeBytes(head_buf, strlen(head), temp);
  segs[1] = qrcodegen_makeNumeric(digits, num_buf);

  static uint8_t scratch[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  if (!qrcodegen_encodeSegmentsAdvanced(segs, 2, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN, 40,
                                        qrcodegen_Mask_AUTO, true, scratch, out)) {
    return -1;
  }
  return (qrcodegen_getSize(out) - 17) / 4;
}

static void test_the_symbol_matches_an_independent_encoding() {
  // the longest of the two payloads, so the symbol is at the panel's limit
  static uint8_t got[CHK_CODE_QR_BUFFER_LEN];
  static uint8_t want[qrcodegen_BUFFER_LEN_FOR_VERSION(40)];
  TEST_ASSERT_TRUE(chk_code_symbol('E', STOVE_DIGITS, got));

  int version = reference_symbol('E', STOVE_DIGITS, want);
  TEST_ASSERT_TRUE(version > 0 && version <= CHK_CODE_QR_MAX_VERSION);

  int size = qrcodegen_getSize(want);
  TEST_ASSERT_EQUAL_INT(size, qrcodegen_getSize(got));
  int wrong = 0;
  for (int y = 0; y < size; y++) {
    for (int x = 0; x < size; x++) {
      if (qrcodegen_getModule(got, x, y) != qrcodegen_getModule(want, x, y)) {
        wrong++;
      }
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, wrong, "every module must match an encoding with separate buffers");
}

static void test_a_link_past_the_panel_is_refused() {
  // 369 digits is the most a version 9 symbol takes behind the prefix; a
  // longer payload must be refused rather than drawn as a symbol that does
  // not fit
  static char digits[512];
  for (int i = 0; i < 420; i++) {
    digits[i] = (char)('0' + (i * 7) % 10);
  }
  digits[420] = '\0';

  static uint8_t out[CHK_CODE_QR_BUFFER_LEN];
  TEST_ASSERT_FALSE(chk_code_symbol('A', digits, out));

  // and something that is not digits at all
  TEST_ASSERT_FALSE(chk_code_symbol('A', "12x4", out));
  TEST_ASSERT_FALSE(chk_code_symbol('A', "", out));
}

void suite_chk_code() {
  RUN_TEST(test_the_symbol_matches_an_independent_encoding);
  RUN_TEST(test_a_link_past_the_panel_is_refused);
  RUN_TEST(test_a_ventilation_payload_matches_the_page);
  RUN_TEST(test_a_stove_payload_matches_the_page);
  RUN_TEST(test_a_long_check_loses_resolution_rather_than_failing);
  RUN_TEST(test_an_unknown_letter_is_refused);
  RUN_TEST(test_a_field_out_of_range_is_refused);
  RUN_TEST(test_the_digits_are_only_digits);
}
