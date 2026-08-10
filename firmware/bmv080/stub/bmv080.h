#ifndef BMV080_H_
#define BMV080_H_

#include <stdint.h>
#include <stdbool.h>

// This is a build stub for the license-gated BMV080 SDK, declaring the API
// surface used by the firmware. It is selected by the component when the SDK
// is absent, so that builds succeed without it. The stub fails on open, which
// the driver treats as an absent sensor. All constant values are arbitrary
// and make no claim to match the SDK.

typedef void* bmv080_handle_t;
typedef void* bmv080_sercom_handle_t;

typedef enum {
  E_BMV080_OK = 0,
  E_BMV080_WARNING_FIFO_EVENTS_OVERFLOW = 4,
  E_BMV080_WARNING_FIFO_SW_BUFFER_SIZE = 208,
  E_BMV080_WARNING_FIFO_HW_BUFFER_SIZE = 209,
  E_BMV080_ERROR_STUBBED = 255,
} bmv080_status_code_t;

typedef enum {
  E_BMV080_MEASUREMENT_ALGORITHM_BALANCED = 2,
} bmv080_measurement_algorithm_t;

typedef enum {
  E_BMV080_DUTY_CYCLING_MODE_0 = 0,
} bmv080_duty_cycling_mode_t;

typedef struct {
  float pm1_mass_concentration;
  float pm2_5_mass_concentration;
  float pm10_mass_concentration;
  bool is_obstructed;
  bool is_outside_measurement_range;
} bmv080_output_t;

typedef int8_t (*bmv080_callback_read_t)(bmv080_sercom_handle_t sercom, uint16_t header, uint16_t* payload,
                                         uint16_t length);
typedef int8_t (*bmv080_callback_write_t)(bmv080_sercom_handle_t sercom, uint16_t header, const uint16_t* payload,
                                          uint16_t length);
typedef int8_t (*bmv080_callback_delay_t)(uint32_t ms);
typedef uint32_t (*bmv080_callback_tick_t)(void);
typedef void (*bmv080_callback_data_ready_t)(bmv080_output_t output, void* param);

bmv080_status_code_t bmv080_open(bmv080_handle_t* handle, bmv080_sercom_handle_t sercom, bmv080_callback_read_t read,
                                 bmv080_callback_write_t write, bmv080_callback_delay_t delay);
bmv080_status_code_t bmv080_reset(bmv080_handle_t handle);
bmv080_status_code_t bmv080_get_driver_version(uint16_t* major, uint16_t* minor, uint16_t* patch, char* git_hash,
                                               int32_t* commits);
bmv080_status_code_t bmv080_get_sensor_id(bmv080_handle_t handle, char* id);
bmv080_status_code_t bmv080_set_parameter(bmv080_handle_t handle, const char* name, const void* value);
bmv080_status_code_t bmv080_start_continuous_measurement(bmv080_handle_t handle);
bmv080_status_code_t bmv080_start_duty_cycling_measurement(bmv080_handle_t handle, bmv080_callback_tick_t tick,
                                                           bmv080_duty_cycling_mode_t mode);
bmv080_status_code_t bmv080_stop_measurement(bmv080_handle_t handle);
bmv080_status_code_t bmv080_serve_interrupt(bmv080_handle_t handle, bmv080_callback_data_ready_t data_ready,
                                            void* param);

#endif  // BMV080_H_
