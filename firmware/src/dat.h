#ifndef DAT_H
#define DAT_H

#include <stdint.h>
#include <stddef.h>

#include <al/sample.h>

#define DAT_MARKS 99

typedef struct __attribute__((packed)) {
  uint16_t num;
  int64_t start;             // ms since 1970
  int32_t marks[DAT_MARKS];  // ms since start
} dat_head_t;

// TODO: Add battery level/voltage?

typedef struct {
  dat_head_t head;
  size_t size;   // samples
  int32_t stop;  // ms since start
  int8_t marks;  // num marks
} dat_file_t;

typedef void (*dat_progress_t)(size_t current, size_t total);

void dat_init();

size_t dat_count();
dat_file_t *dat_get(size_t num);
dat_file_t *dat_find(uint16_t num, int *index);

uint16_t dat_next();
uint16_t dat_create(int64_t start);

void dat_mark(uint16_t num, int32_t offset);
void dat_append(uint16_t num, al_sample_t *samples, size_t count);
void dat_load(uint16_t num, al_sample_t *samples, size_t count, size_t start);
void dat_delete(uint16_t num);

al_sample_source_t dat_source(uint16_t num);

bool dat_import(uint16_t num, int start, dat_progress_t progress);
bool dat_export(uint16_t num, dat_progress_t progress);

void dat_reset();

void dat_enable_usb();
void dat_disable_usb();

void dat_dump(const char *name, const void *data, size_t size);

#endif  // DAT_H
