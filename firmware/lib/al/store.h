#ifndef AL_STORE_H
#define AL_STORE_H

#include <al/sample.h>

#define AL_STORE_NUM_SHORT 180  // 15min-30h (5s/600s)
#define AL_STORE_NUM_LONG 264   // 2.2h-66h (30s/15m)

/**
 * The available stores.
 */
typedef enum {
  AL_STORE_SHORT,
  AL_STORE_LONG,
} al_store_t;

/**
 * Returns the interval at which short term samples are moved to long term storage.
 *
 * @return The interval in seconds.
 */
int al_store_get_interval();

/**
 * Sets the interval at which short term samples are moved to long term storage.
 *
 * Note: The interval range is limited to 30s-15min.
 *
 * @param interval The interval in seconds.
 */
void al_store_set_interval(int interval);

/**
 * Returns the internal epoch time that references the sample offsets.
 *
 * @return The epoch time in milliseconds.
 */
int64_t al_store_get_base();

/**
 * Shifts the time reference of the stored samples, moving them along in time.
 * Used to remediate clock changes without touching the samples.
 *
 * @param diff The time difference in milliseconds.
 */
void al_store_shift(int64_t diff);

/**
 * Adds a sample to the short term store, timed at the given epoch time. Might
 * move a sample to the long term store. The sample offset is managed
 * internally and must be left at zero.
 *
 * @param epoch The epoch time of the sample in milliseconds.
 * @param sample The sample.
 */
void al_store_ingest(int64_t epoch, al_sample_t sample);

/**
 * Returns the first (oldest) sample.
 *
 * @note Might be zero right after reset, check the `co2` field.
 *
 * @return The first sample.
 */
al_sample_t al_store_first();

/**
 * Returns the last (newest) sample.
 *
 * @note Might be zero right after reset, check the `co2` field.
 *
 * @return The sample.
 */
al_sample_t al_store_last();

/**
 * Returns the sample count of a store.
 *
 * @param store The store.
 * @return The sample count.
 */
size_t al_store_count(al_store_t store);

/**
 * Gets a sample from a store.
 *
 * @param store The store.
 * @param num The sample index if positive or the offset from the last sample if negative.
 */
al_sample_t al_store_get(al_store_t store, int num);

/**
 * Returns a combined sample source for both stores.
 *
 * @return The sample source.
 */
al_sample_source_t al_store_source();

#endif  // AL_STORE_H
