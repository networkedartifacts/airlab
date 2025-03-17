#ifndef DAT_H
#define DAT_H

#include <stdint.h>
#include <stddef.h>

#include <al/sensor.h>

#define DAT_MARKS 99

typedef struct __attribute__((packed)) {
  uint16_t num;
  int64_t start;             // ms since 1970
  int32_t marks[DAT_MARKS];  // ms since start
} dat_head_t;

// TODO: Safe space by using int16_t?
// TODO: Add battery level/voltage?

typedef struct __attribute__((packed)) {
  int32_t offset;  // ms since start (24d)
  al_sensor_sample_t sample;
} dat_point_t;

typedef struct {
  dat_head_t head;
  size_t size;   // points
  int32_t stop;  // ms since start
  int8_t marks;  // num marks
} dat_file_t;

typedef struct {
  uint32_t total;
  uint32_t free;
  float usage;
} dat_info_t;

void dat_init();

size_t dat_num_files();
dat_file_t *dat_get_file(size_t num);
dat_info_t dat_info();

uint16_t dat_next();
size_t dat_create(int64_t start);

void dat_mark(uint16_t num, int32_t offset);
void dat_append(uint16_t num, dat_point_t *points, size_t count);
void dat_read(uint16_t num, dat_point_t *points, size_t count, size_t start);
void dat_delete(uint16_t num);

size_t dat_search(uint16_t num, int32_t *needle);
size_t dat_query(uint16_t num, dat_point_t *points, size_t count, int32_t start, int32_t resolution);

void dat_reset();

void dat_enable_usb();
void dat_disable_usb();

void dat_dump(const char *name, const void *data, size_t size);

#endif  // DAT_H
