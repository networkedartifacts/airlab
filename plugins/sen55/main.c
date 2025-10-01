#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../al.h"

#define SEN55 0x69

static uint16_t sen55_bw[4];
static uint16_t sen55_br[4];
static uint8_t sen55_bt[2 + 4 * 3];

static uint8_t sen55_crc(const uint8_t *data, uint16_t count) {
  // crc-8 calculation as defined per datasheet
  uint8_t crc = 0xFF;
  for (uint16_t byte = 0; byte < count; ++byte) {
    crc ^= (data[byte]);
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x31;
      } else {
        crc = (crc << 1);
      }
    }
  }

  return crc;
}

static int sen55_transfer(uint8_t target, uint16_t addr, size_t send, size_t receive, int timeout) {
  // prepare write length
  size_t write = 0;

  // write address
  if (addr != 0) {
    sen55_bt[0] = addr >> 8;
    sen55_bt[1] = addr & 0xFF;
    write += 2;
  }

  // write bytes
  for (size_t i = 0; i < send; i++) {
    sen55_bt[2 + i * 3] = sen55_bw[i] >> 8;
    sen55_bt[2 + i * 3 + 1] = sen55_bw[i] & 0xFF;
    sen55_bt[2 + i * 3 + 2] = sen55_crc(sen55_bt + (2 + i * 3), 2);
    write += 3;
  }

  // run command
  int err = al_i2c(target, sen55_bt, write, sen55_bt, receive * 3, timeout);
  if (err != 0) {
    return err;
  }

  // read bytes
  for (size_t i = 0; i < receive; i++) {
    sen55_br[i] = (sen55_bt[i * 3] << 8) | sen55_bt[i * 3 + 1];
    uint8_t crc = sen55_crc(sen55_bt + (i * 3), 2);
    if (sen55_bt[i * 3 + 2] != crc) {
      return -1;
    }
  }

  return 0;
}

int main() {
  // start periodic measurement
  sen55_transfer(SEN55, 0x0021, 0, 0, 1000);

  for (;;) {
    // read sensor
    sen55_transfer(SEN55, 0x03C4, 0, 0, 1000);
    al_yield(20, 0);  // TODO: Use just a delay?
    sen55_transfer(SEN55, 0, 0, 4, 1000);

    // parse data
    uint16_t pm1 = sen55_br[0];
    uint16_t pm2 = sen55_br[1];
    uint16_t pm4 = sen55_br[2];
    uint16_t pmx = sen55_br[3];

    // format data
    char buffer[128];
    snprintf(buffer, sizeof(buffer), "PM1.0: %u\nPM2.5: %u\nPM4.0: %u\nPM10: %u\n", pm1, pm2, pm4, pmx);

    // display data
    al_clear(0);
    al_write(0, 24, 4, 16, 1, buffer, AL_WRITE_ALIGN_CENTER);

    // wait a bit
    al_yield_result_t res = al_yield(1000, 0);

    // stop on escape
    if (res == AL_YIELD_ESCAPE) {
      break;
    }
  }

  return 0;
}
