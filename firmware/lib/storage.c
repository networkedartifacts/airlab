#include <sys/stat.h>
#include <string.h>
#include <errno.h>
#include <naos.h>

#include <al/core.h>
#include <al/storage.h>

#include "storage.h"

static void al_storage_path(char *path, size_t size, al_storage_type_t type, const char *dir, const char *name) {
  // format path (name may be absent)
  const char *base = type == AL_STORAGE_INT ? AL_STORAGE_INTERNAL : AL_STORAGE_EXTERNAL;
  int ret = name != NULL ? snprintf(path, size, "%s/%s/%s", base, dir, name) : snprintf(path, size, "%s/%s", base, dir);
  if (ret < 0 || (size_t)ret >= size) {
    ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
  }
}

bool al_storage_access(const char *path) {
  // create test file
  FILE *file = fopen(path, "w");
  if (file == NULL) {
    naos_log("al-sto: failed to create test file, error=%d", errno);
    return false;
  }

  // close file
  fclose(file);

  // remove test file
  int ret = remove(path);
  if (ret != 0) {
    naos_log("al-sto: failed to remove test file, error=%d", ret);
    return false;
  }

  return true;
}

void al_storage_init() {
  // only mount the internal file system, as the external disk is backed by
  // PSRAM that is allocated and formatted on demand
  al_storage_internal_init();
  al_storage_external_init();
}

void al_storage_prepare(al_storage_type_t type) {
  if (type == AL_STORAGE_EXT) {
    al_storage_external_ensure();
  }
}

al_storage_info_t al_storage_info(al_storage_type_t type) {
  if (type == AL_STORAGE_INT) {
    return al_storage_internal_info();
  }
  return al_storage_external_info();
}

void al_storage_enable_usb(al_storage_eject_t eject) { al_storage_external_enable_usb(eject); }

void al_storage_disable_usb() { al_storage_external_disable_usb(); }

void al_storage_reset() {
  al_storage_internal_reset();
  al_storage_external_reset();
}

int al_storage_stat(al_storage_type_t type, const char *dir, const char *name) {
  // prepare storage
  al_storage_prepare(type);

  // prepare path
  char path[128];
  al_storage_path(path, sizeof(path), type, dir, name);

  // log
  if (AL_STORAGE_DEBUG) {
    naos_log("al-sto: stat path=%s", path);
  }

  // stat file
  struct stat st;
  int res = stat(path, &st);
  if (res != 0) {
    if (errno == ENOENT) {
      return -1;
    }
    ESP_ERROR_CHECK(errno);
  }

  return (int)st.st_size;
}

bool al_storage_read(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                     size_t length) {
  // prepare storage
  al_storage_prepare(type);

  // prepare path
  char path[128];
  al_storage_path(path, sizeof(path), type, dir, name);

  // log
  if (AL_STORAGE_DEBUG) {
    naos_log("al-sto: read path=%s offset=%d length=%d", path, offset, length);
  }

  // open file
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    if (errno == ENOENT) {
      return false;
    }
    ESP_ERROR_CHECK(errno);
  }

  // seek file
  if (offset > 0) {
    int ret = fseek(file, (long)offset, SEEK_SET);
    if (ret != 0) {
      ESP_ERROR_CHECK(errno);
    }
  }

  // read data
  size_t ret = fread(buf, 1, length, file);
  if (ret != length) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }

  // close file
  fclose(file);

  return true;
}

void *al_storage_load(al_storage_type_t type, const char *dir, const char *name, size_t *size) {
  // get size
  int sz = al_storage_stat(type, dir, name);
  if (sz < 0) {
    return NULL;
  }

  // set size
  *size = (size_t)sz;

  // allocate buffer
  void *buf = al_alloc(*size);

  // read file
  if (!al_storage_read(type, dir, name, buf, 0, *size)) {
    free(buf);
    return NULL;
  }

  return buf;
}

void al_storage_write(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                      size_t length, bool truncate) {
  // prepare storage
  al_storage_prepare(type);

  // ensure directory
  char path[128];
  al_storage_path(path, sizeof(path), type, dir, NULL);
  mkdir(path, 0777);

  // prepare path
  al_storage_path(path, sizeof(path), type, dir, name);

  // log
  if (AL_STORAGE_DEBUG) {
    naos_log("al-sto: write path=%s offset=%d length=%d truncate=%d", path, offset, length, truncate);
  }

  // open file
  FILE *file = fopen(path, offset == 0 && truncate ? "w" : "r+");
  if (file == NULL) {
    ESP_ERROR_CHECK(errno);
  }

  // seek file
  if (offset > 0) {
    int ret = fseek(file, (long)offset, SEEK_SET);
    if (ret != 0) {
      ESP_ERROR_CHECK(errno);
    }
  }

  // write data
  size_t ret = fwrite(buf, 1, length, file);
  if (ret != length) {
    ESP_ERROR_CHECK(errno);
  }

  // close file
  fclose(file);
}

void al_storage_delete(al_storage_type_t type, const char *dir, const char *name) {
  // prepare storage
  al_storage_prepare(type);

  // prepare path
  char path[128];
  al_storage_path(path, sizeof(path), type, dir, name);

  // log
  if (AL_STORAGE_DEBUG) {
    naos_log("al-sto: delete path=%s", path);
  }

  // remove file
  int ret = remove(path);
  if (ret != 0 && ret != ENOENT) {
    ESP_ERROR_CHECK(errno);
  }
}
