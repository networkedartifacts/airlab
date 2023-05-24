#ifndef PWR_H
#define PWR_H

typedef struct {
  float battery;
  bool usb;
  bool fast;
  bool charging;
} pwr_state_t;

void pwr_init();

pwr_state_t pwr_get();

void pwr_off();
void pwr_sleep(bool deep);

#endif  // PWR_H
