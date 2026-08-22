// Battery life model for the Air Lab.
//
// Estimates the whole-device deep-sleep current from the three power
// relevant parameters (sleep-rate, display-rate, gas-window) and derives a
// runtime from the cell every device ships with:
//
//   mA = floor + scd(sleep) + display_wake/display + sgp(window, sleep)
//
// The coefficients and the battery-life ladder come from pwr_model.inc,
// which is generated from bench power measurements and shared with the
// Air Lab Console and the app. The same formula is hand-written in each
// consumer; the host tests pin this implementation to the canonical
// per-rung runtimes.
//
// Not modelled: the cost of WiFi/BLE sync, any time the device spends
// awake, and PM measurements while dozing (pm-rate), whose sleeping cost
// has not been measured in isolation yet. Estimates therefore hold for a
// device that only sleeps and measures.

#include "pwr.h"
#include "pwr_model.inc"

// see pwr.h: it only duty cycles the SGP41 from the single-shot regime on,
// and clamps the window to at least 10s and at most half the sleep interval
int32_t pwr_gas_window(int32_t window, int32_t sleep) {
  // disabled entirely
  if (window < 0) {
    return -1;
  }

  // continuous, either by request or because the sensor never sleeps below
  // the single-shot regime
  if (window == 0 || sleep < PWR_SINGLE_SHOT_S) {
    return 0;
  }

  // clamp to the allowed range
  int32_t max = sleep / 2;
  if (window < PWR_GAS_WINDOW_MIN_S) {
    window = PWR_GAS_WINDOW_MIN_S;
  } else if (window > max) {
    window = max;
  }

  return window;
}

double pwr_current(int32_t sleep, int32_t display, int32_t gas) {
  // clamp to what the firmware accepts
  if (sleep < PWR_SLEEP_MIN_S) {
    sleep = PWR_SLEEP_MIN_S;
  } else if (sleep > PWR_SLEEP_MAX_S) {
    sleep = PWR_SLEEP_MAX_S;
  }
  if (display < PWR_DISPLAY_MIN_S) {
    display = PWR_DISPLAY_MIN_S;
  } else if (display > PWR_DISPLAY_MAX_S) {
    display = PWR_DISPLAY_MAX_S;
  }

  // start at the quiet floor
  double ma = PWR_FLOOR_MA;

  // add the CO2 sensor, whose mode depends on the interval
  if (sleep >= PWR_CYCLED_S) {
    ma += (PWR_SCD_SHOT_MC + PWR_SCD_CYCLE_MC) / sleep;
  } else if (sleep >= PWR_SINGLE_SHOT_S) {
    ma += PWR_SCD_SHOT_MC / sleep + PWR_SCD_SHOT_IDLE_MA;
  } else if (sleep >= PWR_LOW_POWER_S) {
    ma += PWR_SCD_LOW_POWER_MA;
  } else {
    ma += PWR_SCD_PERIODIC_MA;
  }

  // add one display refresh per display interval
  ma += PWR_DISPLAY_WAKE_MC / display;

  // add the gas sensor
  int32_t window = pwr_gas_window(gas, sleep);
  if (window == 0) {
    ma += PWR_SGP_HEATER_MA;
  } else if (window > 0) {
    ma += PWR_SGP_HEATER_MA * window / sleep + PWR_SGP_IDLE_MA;
  }

  return ma;
}

double pwr_days(int32_t sleep, int32_t display, int32_t gas) {
  return PWR_CAPACITY_MAH / pwr_current(sleep, display, gas) / 24;
}

int pwr_num_rungs(void) { return PWR_LADDER_NUM; }

pwr_rung_t pwr_rung(int index) { return pwr_ladder[index]; }
