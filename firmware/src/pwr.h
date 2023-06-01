#ifndef PWR_H
#define PWR_H

typedef struct {
  float battery;
  bool usb;
  bool fast;
  bool charging;
} pwr_state_t;

typedef enum {
  PWR_NONE,
  PWR_TIMEOUT,
  PWR_UNLOCK,
} pwr_cause_t;

void pwr_init();

pwr_state_t pwr_get();

void pwr_off();
pwr_cause_t pwr_sleep(bool deep, uint64_t timeout);
pwr_cause_t pwr_cause();

#endif  // PWR_H
