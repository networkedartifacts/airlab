#ifndef AL_UTILS_H
#define AL_UTILS_H

/**
 * Map number linearly from one range to another. The input range must not be empty.
 *
 * @param num The number.
 * @param in_min The input lower range.
 * @param in_max The input upper range.
 * @param out_min The output lower range.
 * @param out_max The output upper range.
 * @return The mapped value.
 */
static inline float al_map(float num, float in_min, float in_max, float out_min, float out_max) {
  return (num - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

/**
 * Map number linearly from one range to another, constraining it to the output range.
 *
 * @param num The number.
 * @param in_min The input lower range.
 * @param in_max The input upper range.
 * @param out_min The output lower range.
 * @param out_max The output upper range.
 * @return The mapped and constrained value, or out_min if the input range is empty.
 */
static inline float al_safe_map(float num, float in_min, float in_max, float out_min, float out_max) {
  if (num <= in_min || in_min >= in_max) {
    return out_min;
  } else if (num >= in_max) {
    return out_max;
  }
  return al_map(num, in_min, in_max, out_min, out_max);
}

#endif  // AL_UTILS_H
