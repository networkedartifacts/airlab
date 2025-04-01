#ifndef HMI_H
#define HMI_H

typedef enum {
  HMI_FLAG_OFF,
  HMI_FLAG_MODAL,
  HMI_FLAG_PROCESS,
  HMI_FLAG_MAX,
} hmi_flag_t;

void hmi_init();

void hmi_set_flag(hmi_flag_t state);
void hmi_clear_flag(hmi_flag_t state);

#endif  // HMI_H
