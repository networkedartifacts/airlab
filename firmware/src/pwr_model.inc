// GENERATED battery-life model data @ c49d7d2 -- do not edit, it is
// regenerated from the canonical power model alongside the other
// surfaces. The formula lives in pwr.c, which must define
// pwr_rung_t before including this file.

#define PWR_CAPACITY_MAH 1500.0
#define PWR_FLOOR_MA 0.397
#define PWR_SCD_SHOT_MC 67.0
#define PWR_SCD_CYCLE_MC 75.0
#define PWR_DISPLAY_WAKE_MC 73.0
#define PWR_SCD_SHOT_IDLE_MA 0.673
#define PWR_SCD_LOW_POWER_MA 2.796
#define PWR_SCD_PERIODIC_MA 14.176
#define PWR_SGP_HEATER_MA 2.87
#define PWR_SGP_IDLE_MA 0.105
#define PWR_CYCLED_S 180
#define PWR_SINGLE_SHOT_S 60
#define PWR_LOW_POWER_S 30
#define PWR_SLEEP_MIN_S 5
#define PWR_SLEEP_MAX_S 600
#define PWR_DISPLAY_MIN_S 60
#define PWR_DISPLAY_MAX_S 300
#define PWR_GAS_WINDOW_MIN_S 10

static const pwr_rung_t pwr_ladder[] = {
    {5, 60, 0},  // 3.3 days
    {30, 60, 0},  // 8.6 days
    {60, 60, 0},  // 10.0 days
    {180, 180, 0},  // 14.0 days
    {600, 300, 0},  // 16.7 days
    {600, 300, 300},  // 25.9 days
    {600, 300, 180},  // 33.9 days
    {600, 300, 120},  // 40.2 days
    {600, 300, 70},  // 47.5 days
    {600, 300, 25},  // 56.7 days
    {600, 300, -1},  // 71.3 days
};

#define PWR_LADDER_NUM ((int)(sizeof(pwr_ladder) / sizeof(pwr_rung_t)))
