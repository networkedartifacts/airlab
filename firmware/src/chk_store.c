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

// the record being written, if any
static chk_store_head_t chk_store_open_head;
static bool chk_store_is_open = false;

static naos_param_t chk_store_params[] = {
    {.name = "check-counter", .type = NAOS_LONG, .sync_l = &chk_store_counter, .default_l = 0},
};

static void chk_store_name(char *name, size_t len, uint16_t num) {
  snprintf(name, len, CHK_STORE_NAME_FMT, num);
}

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

  // records left open by a check that never finished, removed once the
  // directory has been walked rather than from under the walk
  char abandoned[8][32];
  size_t num_abandoned = 0;

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

    // a record still open belongs to the check in progress, if its context
    // survived the reset, and to nothing at all otherwise
    if (head.magic == CHK_STORE_MAGIC_OPEN && head.version == CHK_STORE_VERSION) {
      if (!chk_store_is_open && head.num != 0 && head.num == chk_context()->file) {
        chk_store_open_head = head;
        chk_store_is_open = true;
      } else if (num_abandoned < sizeof(abandoned) / sizeof(abandoned[0]) &&
                 strlen(entry->d_name) < sizeof(abandoned[0])) {
        memcpy(abandoned[num_abandoned], entry->d_name, strlen(entry->d_name) + 1);
        num_abandoned++;
      }
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

  // drop what no check will ever finish
  for (size_t i = 0; i < num_abandoned; i++) {
    naos_log("chk: dropping unfinished record: %s", abandoned[i]);
    al_storage_delete(AL_STORAGE_INT, CHK_STORE_DIR, abandoned[i]);
  }

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

uint16_t chk_store_open(const chk_t *c, uint8_t signal, uint8_t cadence) {
  if (c == NULL) {
    return 0;
  }

  // a record still open belongs to a check that did not finish
  if (chk_store_is_open) {
    chk_store_discard(chk_store_open_head.num);
  }

  // a ring rather than a refusal: checks are cheap and repeatable, and telling
  // someone mid-trial that they cannot run another is a poor answer. The link
  // a shared result travels in keeps working whatever happens here.
  if (chk_store_length >= CHK_STORE_FILES) {
    chk_store_delete(chk_store_files[0].head.num);
  }

  // take the next number, skipping zero, which means none
  chk_store_counter++;
  if ((uint16_t)chk_store_counter == 0) {
    chk_store_counter++;
  }
  naos_set_l("check-counter", chk_store_counter);
  uint16_t num = (uint16_t)chk_store_counter;

  // build the head, which says what the check is but not yet what it found
  chk_store_open_head = (chk_store_head_t){
      .magic = CHK_STORE_MAGIC_OPEN,
      .version = CHK_STORE_VERSION,
      .num = num,
      .start = c->start,
      .check = c->id,
      .signal = signal,
      .cadence = cadence,
      .marks = 0,
      .count = 0,
      .recording = 0,
  };

  // write it, which creates the file
  char name[32];
  chk_store_name(name, sizeof(name), num);
  al_storage_write(AL_STORAGE_INT, CHK_STORE_DIR, name, &chk_store_open_head, 0, sizeof(chk_store_open_head), true);
  chk_store_is_open = true;

  return num;
}

uint16_t chk_store_pending(void) {
  return chk_store_is_open ? chk_store_open_head.num : 0;
}

size_t chk_store_append(uint16_t num, const float *samples, size_t count) {
  if (!chk_store_is_open || chk_store_open_head.num != num || samples == NULL) {
    return 0;
  }

  // no more than the record holds
  size_t room = CHK_STORE_MAX_SAMPLES - chk_store_open_head.count;
  if (count > room) {
    count = room;
  }
  if (count == 0) {
    return 0;
  }

  // append the samples, then the count: the head on flash is what a resume
  // after a deep sleep reads back, so it has to say how many are there
  char name[32];
  chk_store_name(name, sizeof(name), num);
  al_storage_write(AL_STORAGE_INT, CHK_STORE_DIR, name, (void *)samples,
                   sizeof(chk_store_head_t) + chk_store_open_head.count * sizeof(float), count * sizeof(float), false);
  chk_store_open_head.count = (uint16_t)(chk_store_open_head.count + count);
  al_storage_write(AL_STORAGE_INT, CHK_STORE_DIR, name, &chk_store_open_head, 0, sizeof(chk_store_open_head), false);

  return count;
}

bool chk_store_finish(uint16_t num, const chk_t *c) {
  if (!chk_store_is_open || chk_store_open_head.num != num || c == NULL) {
    return false;
  }

  // a curve needs at least two points
  if (chk_store_open_head.count < 2) {
    chk_store_discard(num);
    return false;
  }

  // complete the head with what the check found
  chk_store_head_t head = chk_store_open_head;
  head.magic = CHK_STORE_MAGIC;
  head.marks = 0;
  for (size_t i = 0; i < CHK_MARKS; i++) {
    head.bounds[i] = c->marks[i];
    if (c->marks[i] != 0) {
      head.marks = (uint8_t)(i + 1);
    }
  }
  memcpy(head.result, c->result, sizeof(head.result));

  // seal it
  char name[32];
  chk_store_name(name, sizeof(name), num);
  al_storage_write(AL_STORAGE_INT, CHK_STORE_DIR, name, &head, 0, sizeof(head), false);
  chk_store_is_open = false;

  // and index it
  if (chk_store_length >= CHK_STORE_FILES) {
    chk_store_delete(chk_store_files[0].head.num);
  }
  chk_store_files[chk_store_length++] = (chk_store_file_t){.head = head};

  return true;
}

void chk_store_discard(uint16_t num) {
  if (!chk_store_is_open || chk_store_open_head.num != num) {
    return;
  }

  char name[32];
  chk_store_name(name, sizeof(name), num);
  al_storage_delete(AL_STORAGE_INT, CHK_STORE_DIR, name);
  chk_store_is_open = false;
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
  chk_store_name(name, sizeof(name), num);
  if (!al_storage_read(AL_STORAGE_INT, CHK_STORE_DIR, name, out, sizeof(chk_store_head_t), count * sizeof(float))) {
    return 0;
  }

  return count;
}

void chk_store_delete(uint16_t num) {
  char name[32];
  chk_store_name(name, sizeof(name), num);

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
