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
  int16_t pm;   // PM2.5 ug/m3 (shifted by 10, flagged, -1 = no reading)
} al_sample_t;

/**
 * The available sample fields. The values are part of the plugin API, new
 * fields must be appended to keep existing values stable.
 */
typedef enum {
  AL_SAMPLE_CO2,
  AL_SAMPLE_TMP,
  AL_SAMPLE_HUM,
  AL_SAMPLE_VOC,
  AL_SAMPLE_NOX,
  AL_SAMPLE_PRS,
  AL_SAMPLE_OFF,
  AL_SAMPLE_PM,
} al_sample_field_t;

/**
 * The gas index fields (VOC, NOx) store the index (1-500, 0 = no reading) in
 * the lower bits and carry additional flags in the upper bits. Consumers of
 * raw samples must mask the value with AL_SAMPLE_GAS_VALUE before use. The
 * learning flag is set while the gas index algorithm is in its initial
 * learning phase, which lasts about 1.45 hours for VOC and 5.7 hours for NOx
 * and restarts on a non-deep-sleep boot (cold boot, flash or crash) or when
 * a disabled SGP is re-enabled. Deep sleep, dock transitions and duty
 * cycling changes do not restart it.
 */
#define AL_SAMPLE_GAS_VALUE 0x03FF     // index value mask
#define AL_SAMPLE_GAS_CYCLED 0x0400    // sampled while the SGP was duty-cycled
#define AL_SAMPLE_GAS_LEARNING 0x0800  // algorithm in initial learning phase

/**
 * The PM field stores the value in the lower bits and carries additional flags
 * in the upper bits, with a negative value meaning no reading is available (no
 * sensor, or none taken yet). Consumers of raw samples must mask the value
 * with AL_SAMPLE_PM_VALUE before use. The obstructed flag is set when the
 * sensor reported a blocked field of view: the reading is still recorded to
 * keep the measurement cadence going, but its value is unreliable.
 */
#define AL_SAMPLE_PM_VALUE 0x3FFF       // value mask (0-1638.3 ug/m3)
#define AL_SAMPLE_PM_OBSTRUCTED 0x4000  // sensor obstructed, value unreliable

/**
 * Sets the altitude in meters used to convert the raw station pressure stored
 * in samples to sea-level-corrected pressure (QNH) when read. Zero disables
 * the correction.
 *
 * @param meters The altitude above sea level.
 */
void al_sample_set_altitude(float meters);

/**
 * Checks if a sample is valid.
 *
 * @return True if the sample is valid.
 */
bool al_sample_valid(al_sample_t);

/**
 * Reads a value from a sample. The gas index fields (VOC, NOx) store zero
 * when no reading is available (algorithm blackout, duty-cycled NOx or a
 * disabled SGP) and are read as NaN in that case, so consumers can detect
 * missing values with isnan() and render them accordingly.
 *
 * @param sample The sample.
 * @param field The field.
 * @return The value.
 */
float al_sample_read(al_sample_t sample, al_sample_field_t field);

/**
 * Reads the flags from a sample.
 *
 * @param sample The sample.
 * @param field The field (AL_SAMPLE_VOC, AL_SAMPLE_NOX or AL_SAMPLE_PM).
 * @return The flag bits, or zero for other fields.
 */
int32_t al_sample_flags(al_sample_t sample, al_sample_field_t field);

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
 * Information about a sample source, obtained as one consistent snapshot.
 */
typedef struct {
  int64_t start;   // epoch time of the first sample (ms)
  int32_t length;  // ms between the first and last sample
  size_t count;    // number of samples
} al_sample_info_t;

/**
 * A sample source. Sample offsets are relative to the source start.
 *
 * @param ctx The context.
 * @param info A function to get information about the source.
 * @param read A function to read samples.
 */
typedef struct {
  void *ctx;
  al_sample_info_t (*info)(void *ctx);
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
 * Counts the number of samples in the given range.
 *
 * @param source The source to count.
 * @param start The start offset.
 * @param end The end offset.
 * @return The number of samples in the range.
 */
size_t al_sample_count(al_sample_source_t *source, int32_t start, int32_t end);

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
