#ifndef CHK_CODE_H
#define CHK_CODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qrcodegen.h"

// The result payload: the bytes behind the QR code, as one decimal number.
//
// A result URL is <prefix><LETTER><digits>. The lowercase prefix and the
// letter ride in a byte-mode QR segment and the digits in a numeric-mode one,
// which is what makes a result fit the screen. The letter names a generation
// of the layout and the header names the check, so the URL alone is enough to
// read a result back and there is no database behind the page.
//
// Two things are named for good. A check id names a check, and the letter
// names the layout of every check's fields together: a change to any check's
// fields takes the next letter, so old links keep decoding under the one they
// were made with, and a new check is a row under the current letter. Until
// the first code is out in the world there is nothing to keep decoding, so
// the current letter is edited in place while the checks are prototyped.
//
// This is the encoder only. The decoder lives with the page that renders the
// result; the host tests check that this produces the same bytes it does.

// Where a scanned result lands. The lowercase prefix and the format letter
// ride in a byte-mode QR segment, which is why the prefix is short: every
// character of it costs payload.
#define CHK_CODE_PREFIX "https://airlab.today/ac/"

// The layout generation this firmware writes.
#define CHK_CODE_LETTER 'A'

// The check registry the page shares: an id names a check for good, 0 is none.
typedef enum {
  CHK_CODE_VENT = 1,
  CHK_CODE_PURIFIER = 2,
  CHK_CODE_STOVE = 3,
} chk_code_check_t;

// The most a payload may be, which the QR budget rather than this code sets.
#define CHK_CODE_MAX_BYTES 192
#define CHK_CODE_MAX_DIGITS 480
#define CHK_CODE_MAX_SAMPLES 512
#define CHK_CODE_MAX_FIELDS 12

// Seconds between samples, indexed by the header's cadence field. The first
// eight are the cadences a device samples at while awake; the rest are for
// checks that sleep between samples.
extern const int chk_code_cadences[16];

// The room registry the page shares, fixed like the checks: an index names a
// room for good, and the order is the order a picker lists them, likeliest
// first. None is what the device writes until it can know where it stands,
// and the page then says nothing about the room.
typedef enum {
  CHK_CODE_ROOM_NONE = 0,
  CHK_CODE_ROOM_LIVING = 1,
  CHK_CODE_ROOM_BEDROOM = 2,
  CHK_CODE_ROOM_KITCHEN = 3,
  CHK_CODE_ROOM_OFFICE = 4,
  CHK_CODE_ROOM_BATHROOM = 5,
  CHK_CODE_ROOM_KIDS = 6,
  CHK_CODE_ROOM_MEETING = 7,
  CHK_CODE_ROOM_CLASSROOM = 8,
  CHK_CODE_ROOM_HALLWAY = 9,
  CHK_CODE_ROOM_BASEMENT = 10,
  CHK_CODE_ROOM_WORKSHOP = 11,
  CHK_CODE_ROOM_GARAGE = 12,
  CHK_CODE_ROOM_CAR = 13,
  CHK_CODE_ROOM_HOTEL = 14,
  CHK_CODE_ROOM_OUTDOORS = 15,
} chk_code_room_t;

// The offset a device with no time zone set writes, so the page says UTC
// rather than guessing.
#define CHK_CODE_OFFSET_UNKNOWN INT16_MIN

// What every payload carries before the check's own fields.
typedef struct {
  uint8_t check;    // a chk_code_check_t, which also picks the layout
  uint32_t minute;  // minutes since 2025-01-01 in UTC
  int16_t offset;   // the room's UTC offset in minutes east, or CHK_CODE_OFFSET_UNKNOWN
  uint32_t device;  // the last six hex characters of the device id, the name after "AL"
  uint8_t room;     // a chk_code_room_t, CHK_CODE_ROOM_NONE until the device can know
  uint8_t cadence;  // index into chk_code_cadences
} chk_code_meta_t;

// Packs a result into decimal digits. `fields` are the check's own values in
// its own order and natural units, which the layout scales; `samples` are in
// the series' natural unit.
//
// The encoder tries the format's quantisation steps from fine to coarse and
// keeps the first whose payload fits `max_bytes`, so a longer check loses
// resolution rather than failing. The step it settled on is reported. A
// field past its width is stored as the width's maximum, for the same reason.
//
// Returns false when the check is unknown or no step fits.
bool chk_code_pack(const chk_code_meta_t *meta, const float *fields, size_t num_fields, const float *samples,
                   size_t count, size_t max_bytes, char *digits, size_t digits_len, int *step_out, size_t *bytes_out);

// The largest symbol the 296x128 panel shows at two pixels a module with the
// quiet zone inside the margin. The 153-byte budget above is what fits it at
// error correction level M.
#define CHK_CODE_QR_MAX_VERSION 9
#define CHK_CODE_QR_BUFFER_LEN qrcodegen_BUFFER_LEN_FOR_VERSION(CHK_CODE_QR_MAX_VERSION)

// Encodes a result link as a QR symbol: the prefix and letter in a byte-mode
// segment, the digits in a numeric-mode one. `qrcode` receives the symbol and
// must hold CHK_CODE_QR_BUFFER_LEN bytes. Returns false when the link needs a
// symbol larger than the panel shows.
bool chk_code_symbol(const char *digits, uint8_t *qrcode);

#endif  // CHK_CODE_H
