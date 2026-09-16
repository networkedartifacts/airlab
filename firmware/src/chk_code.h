#ifndef CHK_CODE_H
#define CHK_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The result payload: the bytes behind the QR code, as one decimal number.
//
// A result URL is <prefix><LETTER><digits>. The lowercase prefix and the
// letter ride in a byte-mode QR segment and the digits in a numeric-mode one,
// which is what makes a result fit the screen. The letter names the layout, so
// the URL alone is enough to read a result back and there is no database
// behind the page.
//
// This is the encoder only. The decoder lives with the page that renders the
// result; the host tests check that this produces the same bytes it does.

// Where a scanned result lands. The lowercase prefix and the format letter
// ride in a byte-mode QR segment, which is why the prefix is short: every
// character of it costs payload.
#define CHK_CODE_PREFIX "https://airlab.today/ac/"

// The most a payload may be, which the QR budget rather than this code sets.
#define CHK_CODE_MAX_BYTES 192
#define CHK_CODE_MAX_DIGITS 480
#define CHK_CODE_MAX_SAMPLES 512
#define CHK_CODE_MAX_FIELDS 12

// Seconds between samples, indexed by the header's cadence field.
extern const int chk_code_cadences[8];

// What every payload carries before the format's own fields.
typedef struct {
  uint32_t minute;   // minutes since 2025-01-01, device local wall clock
  uint16_t device;   // device id, shown as a hex tag
  uint8_t room;      // index into the page's room names, 0 when unknown
  uint8_t cadence;   // index into chk_code_cadences
} chk_code_meta_t;

// Packs a result into decimal digits. `fields` are the format's own values in
// its own order and natural units, which the format scales; `samples` are in
// the series' natural unit.
//
// The encoder tries the format's quantisation steps from fine to coarse and
// keeps the first whose payload fits `max_bytes`, so a longer check loses
// resolution rather than failing. The step it settled on is reported.
//
// Returns false when the letter is unknown, a field is out of range, or no
// step fits.
bool chk_code_pack(char letter, const chk_code_meta_t *meta, const float *fields, size_t num_fields,
                   const float *samples, size_t count, size_t max_bytes, char *digits, size_t digits_len,
                   int *step_out, size_t *bytes_out);

#endif  // CHK_CODE_H
