#ifndef AL_SAMPLE_H
#define AL_SAMPLE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/**
 * A single sample.
 */
typedef struct __attribute__((packed)) {
  int32_t off;  // ms (~24 days)
  int16_t co2;  // ppm
  int16_t tmp;  // °C (shifted by 100)
  int16_t hum;  // % rH (shifted by 100)
  int16_t voc;  // indexed
  int16_t nox;  // indexed
  int16_t prs;  // hPa
} al_sample_t;

/**
 * The available sample fields.
 */
typedef enum {
  AL_SAMPLE_CO2,
  AL_SAMPLE_TMP,
  AL_SAMPLE_HUM,
  AL_SAMPLE_VOC,
  AL_SAMPLE_NOX,
  AL_SAMPLE_PRS,
} al_sample_field_t;

/**
 * Checks if a sample is valid.
 *
 * @return True if the sample is valid.
 */
bool al_sample_valid(al_sample_t);

/**
 * Reads a value from a sample.
 *
 * @param sample The sample.
 * @param field The field.
 * @return The value.
 */
float al_sample_read(al_sample_t sample, al_sample_field_t field);

/**
 * Linearly interpolates between two samples.
 *
 * @param a Sample A.
 * @param b Sample B.
 * @param offset The offset to interpolate to.
 * @return The interpolated sample.
 */
al_sample_t al_sample_lerp(al_sample_t a, al_sample_t b, int32_t offset);

/**
 * A sample source.
 *
 * @param ctx The context.
 * @param count A function to count the number of samples.
 * @param start A function to get the start time.
 * @param stop A function to get the stop time.
 * @param read A function to read samples.
 */
typedef struct {
  void *ctx;
  size_t (*count)(void *ctx);
  int64_t (*start)(void *ctx);  // epoch
  int32_t (*stop)(void *ctx);   // ms since start
  void (*read)(void *ctx, al_sample_t *samples, size_t num, size_t offset);
} al_sample_source_t;

/**
 * Searches for the first sample with the given offset or greater.
 *
 * @param source The source to search.
 * @param offset The offset to search for.
 * @return The index of the sample or -1 if not found.
 */
int al_sample_search(al_sample_source_t *source, int32_t *offset);

/**
 * Queries samples from a source and interpolates to match the given resolution.
 *
 * @param source The source to query.
 * @param samples The samples to fill.
 * @param count The number of samples to fill.
 * @param start The start offset.
 * @param resolution The resolution to interpolate to.
 * @return The number of samples filled.
 */
size_t al_sample_query(al_sample_source_t *source, al_sample_t *samples, size_t count, int32_t start,
                       int32_t resolution);

/**
 * Pick values from a sample source.
 *
 * @param source The source.
 * @param field The field.
 * @param num The sample index if positive or the offset from the last sample if negative.
 * @param values The values.
 * @param min The minimum value.
 * @param max The maximum value.
 * @return The number of values picked.
 */
size_t al_sample_pick(al_sample_source_t *source, al_sample_field_t field, int num, float *values, float *min,
                      float *max);

#endif  // AL_SAMPLE_H
