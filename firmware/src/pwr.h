#ifndef PWR_H
#define PWR_H

#include <stdint.h>

// A rung of the battery-life ladder: the individual parameters a profile
// writes. The ladder and the model coefficients are generated from the
// battery workspace (see pwr_model.inc).
typedef struct {
  int16_t sleep;
  int16_t display;
  int16_t gas;
} pwr_rung_t;

int pwr_num_rungs(void);
pwr_rung_t pwr_rung(int index);  // index must be in [0, pwr_num_rungs())

// The gas window the firmware actually applies for a sleep interval: -1 when
// the SGP41 is disabled, 0 when its heater runs continuously, otherwise the
// clamped duty-cycle window in seconds. Mirrors al_sensor_set_interval().
int32_t pwr_gas_window(int32_t window, int32_t sleep);

double pwr_current(int32_t sleep, int32_t display, int32_t gas);
double pwr_days(int32_t sleep, int32_t display, int32_t gas);

#endif  // PWR_H
