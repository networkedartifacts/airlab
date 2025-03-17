#ifndef AL_SAMPLE_H
#define AL_SAMPLE_H

#include <stdint.h>
#include <stddef.h>

/**
 * The available sensors.
 */
typedef enum {
  AL_SENSOR_CO2,
  AL_SENSOR_TMP,
  AL_SENSOR_HUM,
  AL_SENSOR_VOC,
  AL_SENSOR_NOX,
  AL_SENSOR_PRS,
} al_sensor_t;

/**
 * A single sample.
 */
typedef struct __attribute__((packed)) {
  int32_t off;  // ms
  float co2;    // ppm
  float tmp;    // °C
  float hum;    // % rH
  float voc;    // indexed
  float nox;    // indexed
  float prs;    // hPa
} al_sample_t;

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
 * @param read A function to read samples.
 */
typedef struct {
  void *ctx;
  size_t (*count)(void *ctx);
  void (*read)(void *ctx, al_sample_t *samples, size_t num, size_t offset);
} al_sample_source_t;

/**
 * Searches for the first sample with the given offset or greater.
 *
 * @param source The source to search.
 * @param offset The offset to search for.
 * @return The index of the sample.
 */
size_t al_sample_search(al_sample_source_t *source, int32_t *offset);

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

#endif  // AL_SAMPLE_H
