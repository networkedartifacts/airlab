#include <ulp_riscv_i2c.h>

#include "shared.h"

al_sensor_hal_err_t al_ulp_transfer(uint8_t target, uint8_t* wd, size_t wl, uint8_t* rd, size_t rl) {
  // set slave address
  ulp_riscv_i2c_master_set_slave_addr(target);

  // write data
  if (wl > 0) {
    ulp_riscv_i2c_master_set_slave_reg_addr(wd[0]);
    if (wl > 1) {
      ulp_riscv_i2c_master_write_to_device(wd + 1, wl - 1);
    }
  }

  // read data
  if (rl > 0) {
    if (wl == 0 || wl > 1) {
      ulp_riscv_i2c_master_set_slave_reg_addr(0);
    }
    ulp_riscv_i2c_master_read_from_device(rd, rl);
  }

  return AL_SENSOR_HAL_OK;
}
