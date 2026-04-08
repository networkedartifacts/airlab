#include "../../al.h"

#define SEN0471_ADDR 0x74
#define SEN0471_TIMEOUT 500

#define CMD_CHANGE_ACQUIRE_MODE 0x78
#define CMD_GET_GAS_CONCENTRATION 0x86

#define MODE_PASSIVE 0x04

typedef struct {
  uint8_t head;
  uint8_t addr;
  uint8_t data[6];
  uint8_t check;
} sensor_packet_t;

static uint8_t sen0471_checksum(const uint8_t *bytes, uint8_t len) {
  uint8_t sum = 0;
  bytes += 1;
  for (uint8_t i = 0; i < (len - 2); ++i) {
    sum += *bytes++;
  }
  return (uint8_t)((~sum) + 1);
}

static sensor_packet_t sen0471_make_packet(uint8_t command, uint8_t arg0, uint8_t arg1, uint8_t arg2, uint8_t arg3) {
  sensor_packet_t packet = {
      .head = 0xFF,
      .addr = 0x01,
      .data = {command, arg0, arg1, arg2, arg3, 0},
      .check = 0,
  };
  packet.check = sen0471_checksum((const uint8_t *)&packet, 8);
  return packet;
}

static int sen0471_write_packet(sensor_packet_t packet) {
  uint8_t buffer[1 + sizeof(sensor_packet_t)] = {0};
  memcpy(buffer + 1, &packet, sizeof(packet));
  return al_i2c(SEN0471_ADDR, buffer, sizeof(buffer), 0, 0, SEN0471_TIMEOUT);
}

static int sen0471_read_reply(uint8_t *reply, size_t len) {
  uint8_t reg = 0x00;
  int err = al_i2c(SEN0471_ADDR, &reg, 1, 0, 0, SEN0471_TIMEOUT);
  if (err != 0) {
    return err;
  }
  return al_i2c(SEN0471_ADDR, 0, 0, reply, (int)len, SEN0471_TIMEOUT);
}

static bool sen0471_read_command_reply(uint8_t command, uint8_t *reply) {
  sensor_packet_t packet = sen0471_make_packet(command, 0, 0, 0, 0);
  if (sen0471_write_packet(packet) != 0) {
    return false;
  }
  al_delay(10);
  if (sen0471_read_reply(reply, 9) != 0) {
    return false;
  }
  return sen0471_checksum(reply, 8) == reply[8];
}

static bool sen0471_set_passive_mode(void) {
  uint8_t reply[9] = {0};
  sensor_packet_t packet = sen0471_make_packet(CMD_CHANGE_ACQUIRE_MODE, MODE_PASSIVE, 0, 0, 0);
  if (sen0471_write_packet(packet) != 0) {
    return false;
  }
  al_delay(10);
  if (sen0471_read_reply(reply, sizeof(reply)) != 0) {
    return false;
  }
  return sen0471_checksum(reply, 8) == reply[8] && reply[2] == 1;
}

static float sen0471_decode_concentration(const uint8_t *reply) {
  float concentration = (float)((reply[2] << 8) | reply[3]);
  switch (reply[5]) {
    case 1:
      concentration *= 0.1f;
      break;
    case 2:
      concentration *= 0.01f;
      break;
    default:
      break;
  }
  return concentration;
}

static float sen0471_compensate_no2_ppm(float raw_ppm, float temp_c) {
  float compensated = 0.0f;
  if (temp_c > -20.0f && temp_c <= 0.0f) {
    compensated = raw_ppm / (0.005f * temp_c + 0.9f) - (-0.0025f * temp_c + 0.005f);
  } else if (temp_c > 0.0f && temp_c <= 20.0f) {
    compensated = raw_ppm / (0.005f * temp_c + 0.9f) - (0.005f * temp_c + 0.005f);
  } else if (temp_c > 20.0f && temp_c <= 40.0f) {
    compensated = raw_ppm / (0.005f * temp_c + 0.9f) - (0.0025f * temp_c + 0.1f);
  }

  if (compensated < 0.0f) {
    return 0.0f;
  }
  return compensated;
}

static bool sen0471_read_gas_measurement(uint8_t *gas_type, float *concentration) {
  uint8_t reply[9] = {0};
  if (!sen0471_read_command_reply(CMD_GET_GAS_CONCENTRATION, reply)) {
    return false;
  }
  *gas_type = reply[4];
  *concentration = sen0471_decode_concentration(reply);
  return true;
}

static void draw_message(const char *message) {
  al_clear(0);
  al_write(AL_W / 2, AL_H / 2 - 20, 4, 16, 1, message, AL_WRITE_ALIGN_CENTER);
}

int main() {
  while (!sen0471_set_passive_mode()) {
    draw_message("Failed to switch\nsensor to passive mode");
    if (al_yield(1000, 0) == AL_YIELD_ESCAPE) {
      return 0;
    }
  }

  for (;;) {
    uint8_t gas_type = 0;
    float raw_ppm = 0.0f;
    float temp_c = al_info(AL_INFO_SENSOR_TEMPERATURE);
    char buffer[AL_H];

    if (!sen0471_read_gas_measurement(&gas_type, &raw_ppm)) {
      draw_message("Gas read failed");
    } else {
      float compensated_ppm = sen0471_compensate_no2_ppm(raw_ppm, temp_c);
      snprintf(buffer, sizeof(buffer), "NO2: %.2f ppm\nRaw: %.2f ppm", compensated_ppm, raw_ppm);
      draw_message(buffer);
    }

    if (al_yield(1000, 0) == AL_YIELD_ESCAPE) {
      break;
    }
  }

  return 0;
}
