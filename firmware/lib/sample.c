#include <string.h>

#include <al/sample.h>

#define AL_SAMPLE_QUERY_BATCH 32

#define AL_SAMPLE_LERP(a, b, f) (a * (1.f - f) + (b * f))

float al_sample_read(al_sample_t sample, al_sensor_t sensor) {
  // return value
  switch (sensor) {
    case AL_SENSOR_CO2:
      return sample.co2;
    case AL_SENSOR_TMP:
      return sample.tmp;
    case AL_SENSOR_HUM:
      return sample.hum;
    case AL_SENSOR_VOC:
      return sample.voc;
    case AL_SENSOR_NOX:
      return sample.nox;
    case AL_SENSOR_PRS:
      return sample.prs;
    default:
      return 0;
  }
}

al_sample_t al_sample_lerp(al_sample_t a, al_sample_t b, int32_t offset) {
  // calculate factor
  float f = 1.f / (float)(b.off - a.off) * (float)(offset - a.off);

  return (al_sample_t){
      .off = offset,
      .co2 = AL_SAMPLE_LERP(a.co2, b.co2, f),
      .tmp = AL_SAMPLE_LERP(a.tmp, b.tmp, f),
      .hum = AL_SAMPLE_LERP(a.hum, b.hum, f),
      .voc = AL_SAMPLE_LERP(a.voc, b.voc, f),
      .nox = AL_SAMPLE_LERP(a.nox, b.nox, f),
      .prs = AL_SAMPLE_LERP(a.prs, b.prs, f),
  };
}

size_t al_sample_search(al_sample_source_t *source, int32_t *offset) {
  // get count
  size_t count = source->count(source->ctx);

  // calculate range
  size_t start = 0;
  size_t end = count - 1;

  // prepare sample
  al_sample_t sample;

  // find first offset that is greater or equal using binary search
  while (start <= end) {
    // determine middle
    size_t middle = (start + end) / 2;

    // read sample
    source->read(source->ctx, &sample, 1, middle);

    // handle result
    if (sample.off < *offset) {
      start = middle + 1;
      if (start >= count) {
        return -1;
      }
    } else {
      if (middle == 0) {
        return 0;
      }
      end = middle - 1;
    }
  }

  // update needle
  *offset = sample.off;

  return start;
}

size_t al_sample_query(al_sample_source_t *source, al_sample_t *samples, size_t count, int32_t start,
                       int32_t resolution) {
  // zero samples
  memset(samples, 0, count * sizeof(al_sample_t));

  // get size
  size_t size = source->count(source->ctx);

  // find beginning of range
  int32_t needle = start;
  size_t index = al_sample_search(source, &needle);
  if (needle > start) {
    index--;
  }

  // prepare batch
  al_sample_t batch[AL_SAMPLE_QUERY_BATCH];
  size_t batch_pos = 0;
  size_t batch_size = 0;

  // fill samples
  int32_t offset = start;
  for (size_t i = 0; i < count; i++) {
    // find next exact or range match
    for (;;) {
      // fill batch
      if (batch_size == 0 || batch_pos >= batch_size - 1) {
        // get length
        size_t length = size - index;
        if (length > AL_SAMPLE_QUERY_BATCH) {
          length = AL_SAMPLE_QUERY_BATCH;
        } else if (length == 0) {
          return i;
        }

        // read batch
        source->read(source->ctx, batch, length, index);
        batch_pos = 0;
        batch_size = length;
      }

      // handle exact match
      if (batch[batch_pos].off == offset) {
        samples[i] = batch[batch_pos];
        break;
      }

      // handle range match
      if (batch[batch_pos + 1].off > offset) {
        samples[i] = al_sample_lerp(batch[batch_pos], batch[batch_pos + 1], offset);
        break;
      }

      // advanced
      index++;
      batch_pos++;
    }

    // increment
    offset += resolution;
  }

  return count;
}

size_t al_sample_pick(al_sample_source_t *source, al_sensor_t sensor, int num, float *values, float *min, float *max) {
  // limit number to count
  int count = (int)source->count(source->ctx);
  if (num > count) {
    num = count;
  }

  // prepare from/to indexes
  int from = 0;
  int to = num;
  if (num < 0) {
    from = num;
    to = 0;
  }

  // fill values
  for (int i = from; i < to; i++) {
    al_sample_t sample;
    source->read(source->ctx, &sample, 1, i);
    values[i] = al_sample_read(sample, sensor);
  }

  // calculate min/max
  if (min != NULL) {
    *min = 9999.f;
  }
  for (size_t i = 0; i < num; i++) {
    if (max != NULL && values[i] > *max) {
      *max = values[i];
    }
    if (min != NULL && values[i] < *min) {
      *min = values[i];
    }
  }

  return num;
}
