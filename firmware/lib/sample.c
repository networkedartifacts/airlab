#include <math.h>
#include <string.h>

#include <al/sample.h>

#define AL_SAMPLE_QUERY_BATCH 32

#define AL_SAMPLE_LERP(a, b, f) ((float)a * (1.f - f) + ((float)b * f))

static float al_sample_prs_factor = 1.f;

void al_sample_set_altitude(float meters) {
  // clamp to a sensible range
  if (meters < -500.f) meters = -500.f;
  if (meters > 9000.f) meters = 9000.f;

  // ISA barometric formula: QNH = P_station / (1 - L*h/T0)^(g*M/(R*L))
  al_sample_prs_factor = powf(1.f - 0.0065f * meters / 288.15f, 5.255f);
}

bool al_sample_valid(al_sample_t sample) {
  // a sample is valid if CO2 is not zero
  return sample.co2 != 0;
}

float al_sample_read(al_sample_t sample, al_sample_field_t field) {
  // return value
  switch (field) {
    case AL_SAMPLE_CO2:
      return (float)sample.co2;
    case AL_SAMPLE_TMP:
      return (float)sample.tmp / 100.f;
    case AL_SAMPLE_HUM:
      return (float)sample.hum / 100.f;
    case AL_SAMPLE_VOC: {
      // mask out the gas flags, a zero index means no reading is available
      // (algorithm blackout, duty-cycled NOx or disabled SGP) and is
      // reported as NaN
      int16_t voc = (int16_t)(sample.voc & AL_SAMPLE_GAS_VALUE);
      return voc != 0 ? (float)voc : NAN;
    }
    case AL_SAMPLE_NOX: {
      int16_t nox = (int16_t)(sample.nox & AL_SAMPLE_GAS_VALUE);
      return nox != 0 ? (float)nox : NAN;
    }
    case AL_SAMPLE_PRS:
      return (float)sample.prs / al_sample_prs_factor;
    case AL_SAMPLE_PM:
      // a negative value means no reading is available (no or obstructed
      // sensor) and is reported as NaN
      return sample.pm >= 0 ? (float)sample.pm / 10.f : NAN;
    case AL_SAMPLE_OFF:
      return (float)sample.off;
    default:
      return 0;
  }
}

al_sample_t al_sample_lerp(al_sample_t a, al_sample_t b, int32_t offset) {
  // calculate factor
  float f = 1.f / (float)(b.off - a.off) * (float)(offset - a.off);

  // split gas indices into values and flags
  int16_t voc_a = (int16_t)(a.voc & AL_SAMPLE_GAS_VALUE);
  int16_t voc_b = (int16_t)(b.voc & AL_SAMPLE_GAS_VALUE);
  int16_t nox_a = (int16_t)(a.nox & AL_SAMPLE_GAS_VALUE);
  int16_t nox_b = (int16_t)(b.nox & AL_SAMPLE_GAS_VALUE);
  int16_t voc_flags = (int16_t)((a.voc | b.voc) & ~AL_SAMPLE_GAS_VALUE);
  int16_t nox_flags = (int16_t)((a.nox | b.nox) & ~AL_SAMPLE_GAS_VALUE);

  return (al_sample_t){
      .off = offset,
      .co2 = AL_SAMPLE_LERP(a.co2, b.co2, f),
      .tmp = AL_SAMPLE_LERP(a.tmp, b.tmp, f),
      .hum = AL_SAMPLE_LERP(a.hum, b.hum, f),
      // propagate the gas index no-data sentinel instead of synthesizing
      // bogus intermediate values at availability boundaries, and combine
      // the flags of both endpoints otherwise
      .voc = (voc_a == 0 || voc_b == 0) ? 0 : (int16_t)AL_SAMPLE_LERP(voc_a, voc_b, f) | voc_flags,
      .nox = (nox_a == 0 || nox_b == 0) ? 0 : (int16_t)AL_SAMPLE_LERP(nox_a, nox_b, f) | nox_flags,
      .prs = AL_SAMPLE_LERP(a.prs, b.prs, f),
      .pm = (a.pm < 0 || b.pm < 0) ? -1 : (int16_t)AL_SAMPLE_LERP(a.pm, b.pm, f),
  };
}

int32_t al_sample_gas_flags(al_sample_t sample, al_sample_field_t field) {
  // return flag bits of gas index fields
  if (field == AL_SAMPLE_VOC) {
    return sample.voc & ~AL_SAMPLE_GAS_VALUE;
  } else if (field == AL_SAMPLE_NOX) {
    return sample.nox & ~AL_SAMPLE_GAS_VALUE;
  }

  return 0;
}

int al_sample_search(al_sample_source_t *source, int32_t *offset) {
  // get count
  int count = (int)source->count(source->ctx);

  // calculate range
  int start = 0;
  int end = count - 1;

  // prepare sample
  al_sample_t sample;

  // find first offset that is greater or equal using binary search
  while (start <= end) {
    // determine middle
    int middle = (start + end) / 2;

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

size_t al_sample_count(al_sample_source_t *source, int32_t start, int32_t end) {
  // find first sample at or after start
  int32_t needle_start = start;
  int first = al_sample_search(source, &needle_start);
  if (first < 0) {
    return 0;
  }

  // find first sample at or after end
  int32_t needle_end = end;
  int last = al_sample_search(source, &needle_end);
  if (last < 0) {
    return source->count(source->ctx) - first;
  }

  return last - first;
}

size_t al_sample_query(al_sample_source_t *source, al_sample_t *samples, size_t count, int32_t start,
                       int32_t resolution) {
  // zero samples
  memset(samples, 0, count * sizeof(al_sample_t));

  // get size
  size_t size = source->count(source->ctx);

  // find beginning of range
  int32_t needle = start;
  int index = al_sample_search(source, &needle);
  if (index == -1) {
    return 0;
  }
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

size_t al_sample_pick(al_sample_source_t *source, al_sample_field_t field, int num, float *values, float *min,
                      float *max) {
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
    values[i] = al_sample_read(sample, field);
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
