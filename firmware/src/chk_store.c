#include <dirent.h>
#include <errno.h>
#include <naos.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <al/core.h>
#include <al/storage.h>
#include <esp_err.h>

#include "chk_store.h"

#define CHK_STORE_DIR "checks"
#define CHK_STORE_NAME_FMT "check-%04u.bin"

static int32_t chk_store_counter = 0;
static chk_store_file_t *chk_store_files;
static size_t chk_store_length = 0;

static naos_param_t chk_store_params[] = {
    {.name = "check-counter", .type = NAOS_LONG, .sync_l = &chk_store_counter, .default_l = 0},
};

void chk_store_init(void) {
  // register params
  for (size_t i = 0; i < NAOS_COUNT(chk_store_params); i++) {
    naos_register(&chk_store_params[i]);
  }

  // allocate files
  chk_store_files = al_calloc(CHK_STORE_FILES, sizeof(chk_store_file_t));
  chk_store_length = 0;

  // open directory
  DIR *dir = opendir(AL_STORAGE_INTERNAL "/" CHK_STORE_DIR);
  if (dir == NULL) {
    return;  // nothing stored yet
  }

  for (;;) {
    // get entry
    struct dirent *entry = readdir(dir);
    if (entry == NULL) {
      break;
    }

    // ignore anything that is not a plain file
    if (entry->d_type != DT_REG) {
      continue;
    }

    // stop once the index is full
    if (chk_store_length >= CHK_STORE_FILES) {
      break;
    }

    // read the head
    chk_store_head_t head = {0};
    if (!al_storage_read(AL_STORAGE_INT, CHK_STORE_DIR, entry->d_name, &head, 0, sizeof(head))) {
      continue;
    }

    // leave a file written by another firmware alone rather than deleting it:
    // a check that cannot be listed is still the user's
    if (head.magic != CHK_STORE_MAGIC || head.version != CHK_STORE_VERSION) {
      naos_log("chk: skipping foreign file: %s", entry->d_name);
      continue;
    }

    chk_store_files[chk_store_length++] = (chk_store_file_t){.head = head};
  }

  closedir(dir);

  // oldest first, so the list reads in the order the checks were run
  for (size_t i = 1; i < chk_store_length; i++) {
    for (size_t j = i; j > 0 && chk_store_files[j].head.num < chk_store_files[j - 1].head.num; j--) {
      chk_store_file_t tmp = chk_store_files[j];
      chk_store_files[j] = chk_store_files[j - 1];
      chk_store_files[j - 1] = tmp;
    }
  }
}

size_t chk_store_count(void) {
  return chk_store_length;
}

chk_store_file_t *chk_store_get(size_t num) {
  if (num >= chk_store_length) {
    return NULL;
  }
  return &chk_store_files[num];
}

uint16_t chk_store_write(const chk_t *c, uint8_t signal, uint8_t cadence, const float *samples, size_t count) {
  if (c == NULL || samples == NULL || count == 0) {
    return 0;
  }
  if (count > CHK_STORE_MAX_SAMPLES) {
    count = CHK_STORE_MAX_SAMPLES;
  }

  // a ring rather than a refusal: checks are cheap and repeatable, and telling
  // someone mid-trial that they cannot run another is a poor answer. The link
  // a shared result travels in keeps working whatever happens here.
  if (chk_store_length >= CHK_STORE_FILES) {
    chk_store_delete(chk_store_files[0].head.num);
  }

  // take the next number
  chk_store_counter++;
  naos_set_l("check-counter", chk_store_counter);
  uint16_t num = (uint16_t)chk_store_counter;

  // build the head
  chk_store_head_t head = {
      .magic = CHK_STORE_MAGIC,
      .version = CHK_STORE_VERSION,
      .num = num,
      .start = c->start,
      .check = c->id,
      .signal = signal,
      .cadence = cadence,
      .marks = 0,
      .count = (uint16_t)count,
      .recording = 0,
  };
  for (size_t i = 0; i < CHK_MARKS; i++) {
    head.bounds[i] = c->marks[i];
    if (c->marks[i] != 0) {
      head.marks = (uint8_t)(i + 1);
    }
  }
  memcpy(head.result, c->result, sizeof(head.result));

  // write it, head first so a torn write leaves a file the scan rejects
  char name[32];
  snprintf(name, sizeof(name), CHK_STORE_NAME_FMT, num);
  al_storage_write(AL_STORAGE_INT, CHK_STORE_DIR, name, &head, 0, sizeof(head), true);
  al_storage_write(AL_STORAGE_INT, CHK_STORE_DIR, name, (void *)samples, sizeof(head), count * sizeof(float),
                   false);

  // and index it
  if (chk_store_length < CHK_STORE_FILES) {
    chk_store_files[chk_store_length++] = (chk_store_file_t){.head = head};
  }

  return num;
}

size_t chk_store_samples(uint16_t num, float *out, size_t max) {
  // find the file
  chk_store_head_t *head = NULL;
  for (size_t i = 0; i < chk_store_length; i++) {
    if (chk_store_files[i].head.num == num) {
      head = &chk_store_files[i].head;
      break;
    }
  }
  if (head == NULL || out == NULL) {
    return 0;
  }

  size_t count = head->count;
  if (count > max) {
    count = max;
  }

  char name[32];
  snprintf(name, sizeof(name), CHK_STORE_NAME_FMT, num);
  if (!al_storage_read(AL_STORAGE_INT, CHK_STORE_DIR, name, out, sizeof(chk_store_head_t), count * sizeof(float))) {
    return 0;
  }

  return count;
}

void chk_store_delete(uint16_t num) {
  char name[32];
  snprintf(name, sizeof(name), CHK_STORE_NAME_FMT, num);

  al_storage_delete(AL_STORAGE_INT, CHK_STORE_DIR, name);

  // drop it from the index, keeping the order
  for (size_t i = 0; i < chk_store_length; i++) {
    if (chk_store_files[i].head.num == num) {
      for (size_t j = i; j + 1 < chk_store_length; j++) {
        chk_store_files[j] = chk_store_files[j + 1];
      }
      chk_store_length--;
      break;
    }
  }
}
