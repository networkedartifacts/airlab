#include <math.h>
#include <string.h>

#include "chk_code.h"

#define CHK_CODE_BLOCK 16  // samples per Rice block
#define CHK_CODE_MAX_Q 64   // longest unary run the encoder will emit

const int chk_code_cadences[8] = {1, 2, 5, 10, 15, 20, 30, 60};

// a field: how many bits it takes and the fixed point it is stored at
typedef struct {
  uint8_t bits;
  float scale;
} chk_code_field_t;

// mark, minute, device, room, cadence
static const chk_code_field_t chk_code_header[] = {
    {1, 1}, {22, 1}, {16, 1}, {4, 1}, {3, 1},
};

// ach, achSe, r2, c0, c1, cout, pre
static const chk_code_field_t chk_code_fields_a[] = {
    {12, 100}, {8, 100}, {7, 100}, {13, 1}, {13, 1}, {11, 1}, {6, 1},
};

// ce, hoodAch, slope1, slope3, c0, noxPeak, pre, pass1, pass2, pass3
static const chk_code_field_t chk_code_fields_e[] = {
    {10, 10}, {12, 100}, {12, 10}, {12, 10}, {13, 1}, {9, 1}, {6, 1}, {10, 1}, {10, 1}, {10, 1},
};

typedef struct {
  char letter;
  const chk_code_field_t *fields;
  size_t num_fields;
  float scale;         // fixed point of a raw sample
  const int *steps;    // quantisation steps, finest first
  size_t num_steps;
} chk_code_format_t;

static const int chk_code_steps_co2[] = {1, 2, 5, 10, 20, 25, 50, 100};

static const chk_code_format_t chk_code_formats[] = {
    {'A', chk_code_fields_a, 7, 1, chk_code_steps_co2, 8},
    {'E', chk_code_fields_e, 10, 1, chk_code_steps_co2, 8},
};

/* Bit writer */

typedef struct {
  uint8_t bytes[CHK_CODE_MAX_BYTES];
  size_t len;
  uint8_t acc;
  int n;
  bool overflow;
} chk_code_writer_t;

static void chk_code_bit(chk_code_writer_t *w, int b) {
  w->acc = (uint8_t)((w->acc << 1) | (b & 1));
  if (++w->n == 8) {
    if (w->len < sizeof(w->bytes)) {
      w->bytes[w->len++] = w->acc;
    } else {
      w->overflow = true;
    }
    w->acc = 0;
    w->n = 0;
  }
}

static bool chk_code_write(chk_code_writer_t *w, uint32_t value, int bits) {
  // a value that does not fit is a programming error, not a long payload
  if (bits < 32 && value >= (1u << bits)) {
    return false;
  }
  for (int i = bits - 1; i >= 0; i--) {
    chk_code_bit(w, (int)((value >> i) & 1));
  }
  return true;
}

static void chk_code_finish(chk_code_writer_t *w) {
  while (w->n != 0) {
    chk_code_bit(w, 0);
  }
}

/* Series */

static uint32_t chk_code_zigzag(int32_t d) {
  return d >= 0 ? (uint32_t)(2 * d) : (uint32_t)(-2 * d - 1);
}

// zigzag deltas, Rice-coded with the parameter chosen per block of sixteen
static bool chk_code_series(chk_code_writer_t *w, const int32_t *quantised, size_t count) {
  for (size_t b = 1; b < count; b += CHK_CODE_BLOCK) {
    size_t end = b + CHK_CODE_BLOCK;
    if (end > count) {
      end = count;
    }

    // choose the parameter that spends the fewest bits on this block
    int best_k = 0;
    uint64_t best = UINT64_MAX;
    for (int k = 0; k < 8; k++) {
      uint64_t bits = 0;
      for (size_t i = b; i < end; i++) {
        bits += (chk_code_zigzag(quantised[i] - quantised[i - 1]) >> k) + 1 + (uint64_t)k;
      }
      if (bits < best) {
        best = bits;
        best_k = k;
      }
    }
    if (!chk_code_write(w, (uint32_t)best_k, 3)) {
      return false;
    }

    // unary quotient, then the low bits
    for (size_t i = b; i < end; i++) {
      uint32_t v = chk_code_zigzag(quantised[i] - quantised[i - 1]);
      uint32_t q = v >> best_k;
      if (q > CHK_CODE_MAX_Q) {
        return false;  // too large a jump; the caller tries a coarser step
      }
      for (uint32_t j = 0; j < q; j++) {
        chk_code_bit(w, 1);
      }
      chk_code_bit(w, 0);
      if (best_k != 0 && !chk_code_write(w, v & ((1u << best_k) - 1), best_k)) {
        return false;
      }
    }
  }
  return true;
}

/* Decimal */

// big-endian bytes as one decimal number, dividing 32-bit limbs by 10^9 a
// round so the work is (bytes / 4) * (digits / 9) steps rather than
// bytes * digits, which matters against the watchdog
static size_t chk_code_decimal(const uint8_t *bytes, size_t n, char *out, size_t out_len) {
  uint32_t limbs[CHK_CODE_MAX_BYTES / 4 + 1];
  size_t nl = (n + 3) / 4;
  for (size_t i = 0; i < nl; i++) {
    limbs[i] = 0;
  }
  for (size_t i = 0; i < n; i++) {
    size_t at = (i + (nl * 4 - n)) / 4;
    limbs[at] = (limbs[at] << 8) | bytes[i];
  }

  char rev[CHK_CODE_MAX_DIGITS];
  size_t start = 0, len = 0;
  while (start < nl) {
    uint64_t rem = 0;
    for (size_t i = start; i < nl; i++) {
      uint64_t cur = (rem << 32) | limbs[i];
      limbs[i] = (uint32_t)(cur / 1000000000u);
      rem = cur % 1000000000u;
    }
    while (start < nl && limbs[start] == 0) {
      start++;
    }
    for (int k = 0; k < 9; k++) {
      if (len >= sizeof(rev)) {
        return 0;
      }
      rev[len++] = (char)('0' + (rem % 10));
      rem /= 10;
      if (start >= nl && rem == 0) {
        break;
      }
    }
  }

  // drop the leading zeros the last round may have added, but keep one digit
  while (len > 1 && rev[len - 1] == '0') {
    len--;
  }
  if (len + 1 > out_len) {
    return 0;
  }
  for (size_t i = 0; i < len; i++) {
    out[i] = rev[len - 1 - i];
  }
  out[len] = '\0';

  return len;
}

/* Packing */

static bool chk_code_pack_fields(chk_code_writer_t *w, const chk_code_field_t *spec, size_t num,
                                 const float *values) {
  for (size_t i = 0; i < num; i++) {
    float v = values != NULL ? values[i] : 0;
    if (isnan(v) || v < 0) {
      v = 0;  // a value the check could not produce reads back as zero
    }
    double scaled = (double)v * spec[i].scale;
    uint32_t stored = (uint32_t)(scaled + 0.5);
    if (spec[i].bits < 32 && stored >= (1u << spec[i].bits)) {
      return false;
    }
    if (!chk_code_write(w, stored, spec[i].bits)) {
      return false;
    }
  }
  return true;
}

static const chk_code_format_t *chk_code_format(char letter) {
  for (size_t i = 0; i < sizeof(chk_code_formats) / sizeof(chk_code_formats[0]); i++) {
    if (chk_code_formats[i].letter == letter) {
      return &chk_code_formats[i];
    }
  }
  return NULL;
}

bool chk_code_pack(char letter, const chk_code_meta_t *meta, const float *fields, size_t num_fields,
                   const float *samples, size_t count, size_t max_bytes, char *digits, size_t digits_len,
                   int *step_out, size_t *bytes_out) {
  const chk_code_format_t *format = chk_code_format(letter);
  if (format == NULL || meta == NULL || samples == NULL || digits == NULL) {
    return false;
  }
  if (count < 1 || count > CHK_CODE_MAX_SAMPLES || count >= 1024) {
    return false;
  }
  if (num_fields != format->num_fields) {
    return false;
  }
  if (max_bytes > CHK_CODE_MAX_BYTES) {
    max_bytes = CHK_CODE_MAX_BYTES;
  }

  static int32_t quantised[CHK_CODE_MAX_SAMPLES];
  static chk_code_writer_t w;

  // the header, which every format shares
  const float header[] = {
      1, (float)meta->minute, (float)meta->device, (float)meta->room, (float)meta->cadence,
  };

  // finest step first, so a short check keeps the curve the device recorded
  for (size_t s = 0; s < format->num_steps; s++) {
    int step = format->steps[s];

    for (size_t i = 0; i < count; i++) {
      double raw = (double)samples[i] * format->scale;
      if (raw < 0) {
        raw = 0;
      }
      quantised[i] = (int32_t)(raw / step + 0.5);
    }
    if (quantised[0] > 65535) {
      continue;  // the first sample is stored raw in sixteen bits
    }

    memset(&w, 0, sizeof(w));
    bool ok = chk_code_pack_fields(&w, chk_code_header, 5, header) &&
              chk_code_pack_fields(&w, format->fields, format->num_fields, fields) &&
              chk_code_write(&w, (uint32_t)count, 10) && chk_code_write(&w, (uint32_t)s, 3) &&
              chk_code_write(&w, (uint32_t)quantised[0], 16) && chk_code_series(&w, quantised, count);
    if (!ok || w.overflow) {
      continue;  // a jump too large for this step, or simply too long
    }
    chk_code_finish(&w);

    if (w.len > max_bytes) {
      continue;
    }

    if (chk_code_decimal(w.bytes, w.len, digits, digits_len) == 0) {
      return false;
    }
    if (step_out != NULL) {
      *step_out = step;
    }
    if (bytes_out != NULL) {
      *bytes_out = w.len;
    }
    return true;
  }

  return false;
}
