#include <assert.h>
#include <printf.h>

#define DAT_TEST 1
#include "../src/dat.h"

int main() {
  // initialize
  dat_init();
  assert(dat_num_files() == 0);
  assert(dat_next() == 1);

  // create file
  size_t n = dat_create(42);
  assert(n == 0);
  dat_file_t *file = dat_get_file(n);
  assert(file != NULL);
  assert(file->head.num == 1);
  assert(file->head.start == 42);
  assert(file->size == 0);

  // append points (single)
  for (int i = 0; i < 4; i++) {
    dat_point_t point = {
        .offset = 7 + i,
        .co2 = (float)(11 + i),
        .tmp = (float)(22 + i),
        .hum = (float)(33 + i),
    };
    dat_append(file->head.num, &point, 1);
    assert(file->size == i + 1);
    assert(file->stop == point.offset);
  }

  // append points (multiple)
  dat_point_t points[4];
  for (int i = 0; i < 4; i++) {
    dat_point_t point = {
        .offset = 77 + i,
        .co2 = (float)(111 + i),
        .tmp = (float)(222 + i),
        .hum = (float)(333 + i),
    };
    points[i] = point;
  }
  dat_append(file->head.num, points, 4);
  assert(file->size == 8);
  assert(file->stop == 80);

  // read points (single)
  for (int i = 0; i < 4; i++) {
    dat_point_t point;
    dat_read(file->head.num, &point, 1, i);
    assert(point.offset == 7 + i);
    assert(point.co2 == 11 + i);
    assert(point.tmp == 22 + i);
    assert(point.hum == 33 + i);
  }

  // read points (multiple)
  dat_read(file->head.num, points, 4, 4);
  for (int i = 0; i < 4; i++) {
    assert(points[i].offset == 77 + i);
    assert(points[i].co2 == 111 + i);
    assert(points[i].tmp == 222 + i);
    assert(points[i].hum == 333 + i);
  }

  // search file (exact)
  int32_t needle = 9;
  size_t index = dat_search(file->head.num, &needle);
  assert(index == 2);

  // search file (range)
  needle = 50;
  index = dat_search(file->head.num, &needle);
  assert(index == 4);

  // search file (before)
  needle = 2;
  index = dat_search(file->head.num, &needle);
  assert(index == 0);

  // search file (after)
  needle = 100;
  index = dat_search(file->head.num, &needle);
  assert(index == -1);

  // query
  dat_point_t result[8] = {0};
  size_t num = dat_query(file->head.num, result, 8, 7, 15);
  assert(num == 5);
  for (int i = 0; i < 5; i++) {
    assert(result[i].offset == 7 + i * 15);
    assert(result[i].co2 > 0);
    assert(result[i].tmp > 0);
    assert(result[i].hum > 0);
  }

  // print result
  for (int i = 0; i < 8; i++) {
    printf("- %u: %.1f %.1f %.1f\n", result[i].offset, result[i].co2,
           result[i].tmp, result[i].hum);
  }

  // re-initialize
  dat_init();
  assert(dat_num_files() == 1);
  assert(dat_next() == 2);
  file = dat_get_file(0);
  assert(file->head.num == 1);
  assert(file->head.start == 42);
  assert(file->size == 8);
  assert(file->stop == 80);

  // delete file
  dat_delete(file->head.num);
  assert(dat_num_files() == 0);
  assert(dat_next() == 2);
}
