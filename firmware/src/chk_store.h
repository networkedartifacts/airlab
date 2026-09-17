#ifndef CHK_STORE_H
#define CHK_STORE_H

#include <al/sample.h>

#include "chk.h"

// Finished checks on flash.
//
// A check's record is opened when its first measurement ends, appended to as
// each further one ends, and sealed at the result. The samples are copied out
// of the device's own short store, which is a ring of fifteen minutes at the
// usual cadence: a check that took its window out only at the result would
// find its start gone once it ran longer than that. Between the copies the
// only state that has to survive is what crosses a deep sleep, and that lives
// in RTC memory: the record's number and the moment of the last sample kept.
//
// The record is therefore the payload uncompressed plus enough provenance to
// rebuild the result: the header alone must be sufficient to redraw the stats,
// the verdict and the code, without reading the samples back. Until it is
// sealed it carries a magic of its own, so a record left open by a check that
// never finished is recognised and dropped rather than listed.
//
// Checks live apart from recordings. The shapes differ — a recording header is
// mostly a ninety-nine entry array of user marks, where a check needs a few
// phase boundaries in slots its flow names and a result block — and the two
// lists should not mix.

#define CHK_STORE_MAGIC 0x4B434C41       // "ALCK"
#define CHK_STORE_MAGIC_OPEN 0x4F434C41  // "ALCO", a record still being written
#define CHK_STORE_VERSION 5              // 5: the head carries the room; 4: the bounds are the slots each flow names
#define CHK_STORE_FILES 64
#define CHK_STORE_MAX_SAMPLES 512

typedef struct __attribute__((packed)) {
  uint32_t magic;             // CHK_STORE_MAGIC, or CHK_STORE_MAGIC_OPEN while open
  uint16_t version;           // CHK_STORE_VERSION
  uint16_t num;               // file number, also the filename
  int64_t start;              // ms since 1970
  int16_t offset;             // the room's UTC offset at start in minutes east, or CHK_CODE_OFFSET_UNKNOWN
  uint8_t check;              // which check, a chk_id_t
  uint8_t room;               // where it ran, a chk_code_room_t, none when not said
  uint8_t signal;             // the field the samples carry
  uint8_t cadence;            // seconds between samples
  uint8_t marks;              // how many phase boundaries are set
  int32_t bounds[CHK_MARKS];  // phase boundaries, ms since start
  float result[CHK_RESULTS];  // the evaluator's outputs
  uint16_t count;             // samples that follow
  uint16_t recording;         // a recording backing this check, 0 for none
} chk_store_head_t;

typedef struct {
  chk_store_head_t head;
} chk_store_file_t;

// Scans the check directory into memory. Files whose magic or version is not
// understood are left alone rather than deleted, so checks written by another
// firmware survive an upgrade even when they cannot be listed. A record left
// open is kept only if it belongs to the check whose context survived in RTC
// memory; any other was abandoned by a crash and is dropped.
void chk_store_init(void);

// How many sealed checks are stored, newest last.
size_t chk_store_count(void);

// The nth sealed check, or NULL. The header alone carries the result.
chk_store_file_t *chk_store_get(size_t num);

// Opens the record of the check in progress, returning its number or zero on
// failure. Only one record is open at a time; opening another drops it.
uint16_t chk_store_open(const chk_t *c, uint8_t signal, uint8_t cadence);

// The number of the record still open, or zero when none is.
uint16_t chk_store_pending(void);

// Appends samples to the open record, up to its capacity, and returns how many
// it took. The count is written through to flash each time, since the copy in
// memory does not survive a deep sleep.
size_t chk_store_append(uint16_t num, const float *samples, size_t count);

// Seals the open record with the check's phase boundaries and result, after
// which it is listed and reopened like any other. A record with fewer than
// two samples is no curve and is dropped instead, returning false.
bool chk_store_finish(uint16_t num, const chk_t *c);

// Drops the open record if it has this number, which is what an abandoned
// check leaves behind. A sealed record is not touched.
void chk_store_discard(uint16_t num);

// Reads a sealed check's samples back, returning how many were read.
size_t chk_store_samples(uint16_t num, float *out, size_t max);

// Removes a sealed check.
void chk_store_delete(uint16_t num);

#endif  // CHK_STORE_H
