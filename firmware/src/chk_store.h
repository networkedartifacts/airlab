#ifndef CHK_STORE_H
#define CHK_STORE_H

#include <al/sample.h>

#include "chk.h"

// Finished checks on flash.
//
// A check writes nothing while it runs: the only state that has to survive is
// what crosses a deep sleep, and that lives in RTC memory. At the end it takes
// a copy of the window it spanned out of the device's own stores, because
// those are rings that turn over — the short store holds three minutes at the
// usual cadence, so a check reopened an hour later would find nothing.
//
// The record is therefore the payload uncompressed plus enough provenance to
// rebuild the result: the header alone must be sufficient to redraw the stats,
// the verdict and the code, without reading the samples back.
//
// Checks live apart from recordings. The shapes differ — a recording header is
// mostly a ninety-nine entry array of user marks, where a check needs three
// phase boundaries and a result block — and the two lists should not mix.

#define CHK_STORE_MAGIC 0x4B434C41  // "ALCK"
#define CHK_STORE_VERSION 1
#define CHK_STORE_FILES 64
#define CHK_STORE_MAX_SAMPLES 512

typedef struct __attribute__((packed)) {
  uint32_t magic;               // CHK_STORE_MAGIC
  uint16_t version;             // CHK_STORE_VERSION
  uint16_t num;                 // file number, also the filename
  int64_t start;                // ms since 1970
  uint8_t check;                // which check, a chk_id_t
  uint8_t signal;               // the field the samples carry
  uint8_t cadence;              // seconds between samples
  uint8_t marks;                // how many phase boundaries are set
  int32_t bounds[CHK_MARKS];    // phase boundaries, ms since start
  float result[CHK_RESULTS];    // the evaluator's outputs
  uint16_t count;               // samples that follow
  uint16_t recording;           // a recording backing this check, 0 for none
} chk_store_head_t;

typedef struct {
  chk_store_head_t head;
} chk_store_file_t;

// Scans the check directory into memory. Files whose magic or version is not
// understood are left alone rather than deleted, so checks written by another
// firmware survive an upgrade even when they cannot be listed.
void chk_store_init(void);

// How many checks are stored, newest last.
size_t chk_store_count(void);

// The nth stored check, or NULL. The header alone carries the result.
chk_store_file_t *chk_store_get(size_t num);

// Writes a finished check, returning its file number or zero on failure. The
// samples are copied from the caller, which read them out of the device store.
uint16_t chk_store_write(const chk_t *c, uint8_t signal, uint8_t cadence, const float *samples, size_t count);

// Reads a stored check's samples back, returning how many were read.
size_t chk_store_samples(uint16_t num, float *out, size_t max);

// Removes a stored check.
void chk_store_delete(uint16_t num);

#endif  // CHK_STORE_H
