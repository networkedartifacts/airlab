#include "bmv080.h"

// See bmv080.h: all functions fail, and open never yields a handle, so the
// remaining functions are effectively unreachable.

bmv080_status_code_t bmv080_open(bmv080_handle_t* handle, bmv080_sercom_handle_t sercom, bmv080_callback_read_t read,
                                 bmv080_callback_write_t write, bmv080_callback_delay_t delay) {
  (void)sercom;
  (void)read;
  (void)write;
  (void)delay;
  *handle = 0;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_reset(bmv080_handle_t handle) {
  (void)handle;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_get_driver_version(uint16_t* major, uint16_t* minor, uint16_t* patch, char* git_hash,
                                               int32_t* commits) {
  *major = 0;
  *minor = 0;
  *patch = 0;
  *git_hash = 0;
  *commits = 0;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_get_sensor_id(bmv080_handle_t handle, char* id) {
  (void)handle;
  *id = 0;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_set_parameter(bmv080_handle_t handle, const char* name, const void* value) {
  (void)handle;
  (void)name;
  (void)value;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_start_continuous_measurement(bmv080_handle_t handle) {
  (void)handle;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_start_duty_cycling_measurement(bmv080_handle_t handle, bmv080_callback_tick_t tick,
                                                           bmv080_duty_cycling_mode_t mode) {
  (void)handle;
  (void)tick;
  (void)mode;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_stop_measurement(bmv080_handle_t handle) {
  (void)handle;
  return E_BMV080_ERROR_STUBBED;
}

bmv080_status_code_t bmv080_serve_interrupt(bmv080_handle_t handle, bmv080_callback_data_ready_t data_ready,
                                            void* param) {
  (void)handle;
  (void)data_ready;
  (void)param;
  return E_BMV080_ERROR_STUBBED;
}
