#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

#include <esp_err.h>
#include <al/core.h>
#include <al/storage.h>

#include "sig.h"

// shared host shim implementations for units under test

/* Core */

void *al_alloc(size_t size) { return malloc(size); }

void *al_calloc(size_t count, size_t size) { return calloc(count, size); }

/* Signals */

void sig_dispatch(sig_event_t event) { (void)event; }

/* Storage */

uint32_t al_storage_test_free[2] = {4 * 1024 * 1024, 4 * 1024 * 1024};

static const char *fs_root(al_storage_type_t type) {
  return type == AL_STORAGE_INT ? AL_STORAGE_INTERNAL : AL_STORAGE_EXTERNAL;
}

void al_storage_prepare(al_storage_type_t type) { (void)type; }

al_storage_info_t al_storage_info(al_storage_type_t type) {
  return (al_storage_info_t){.total = 4 * 1024 * 1024, .free = al_storage_test_free[type]};
}

void al_storage_enable_usb(al_storage_eject_t eject) { (void)eject; }

void al_storage_disable_usb() {}

int al_storage_stat(al_storage_type_t type, const char *dir, const char *name) {
  char path[128];
  snprintf(path, sizeof(path), "%s/%s/%s", fs_root(type), dir, name);
  struct stat info;
  if (stat(path, &info) != 0) {
    return -1;
  }
  return (int)info.st_size;
}

bool al_storage_read(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                     size_t length) {
  char path[128];
  snprintf(path, sizeof(path), "%s/%s/%s", fs_root(type), dir, name);
  FILE *file = fopen(path, "r");
  if (file == NULL) {
    return false;
  }
  if (fseek(file, (long)offset, SEEK_SET) != 0) {
    ESP_ERROR_CHECK(errno);
  }
  if (fread(buf, 1, length, file) != length) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  fclose(file);
  return true;
}

void al_storage_write(al_storage_type_t type, const char *dir, const char *name, void *buf, size_t offset,
                      size_t length, bool truncate) {
  // ensure directories
  char path[128];
  mkdir(AL_STORAGE_ROOT, 0777);
  mkdir(fs_root(type), 0777);
  snprintf(path, sizeof(path), "%s/%s", fs_root(type), dir);
  mkdir(path, 0777);

  // write file, matching the semantics of the real implementation
  snprintf(path, sizeof(path), "%s/%s/%s", fs_root(type), dir, name);
  FILE *file = fopen(path, offset == 0 && truncate ? "w" : "r+");
  if (file == NULL) {
    ESP_ERROR_CHECK(errno);
    return;
  }
  if (fseek(file, (long)offset, SEEK_SET) != 0) {
    ESP_ERROR_CHECK(errno);
  }
  if (fwrite(buf, 1, length, file) != length) {
    ESP_ERROR_CHECK(ESP_FAIL);
  }
  fclose(file);
}

void al_storage_delete(al_storage_type_t type, const char *dir, const char *name) {
  char path[128];
  snprintf(path, sizeof(path), "%s/%s/%s", fs_root(type), dir, name);
  remove(path);
}

static void fs_wipe(const char *path) {
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return;
  }
  for (;;) {
    struct dirent *entry = readdir(dir);
    if (entry == NULL) {
      break;
    }
    if (entry->d_type != DT_REG) {
      continue;
    }
    char file[512];
    snprintf(file, sizeof(file), "%s/%s", path, entry->d_name);
    remove(file);
  }
  closedir(dir);
}

void al_storage_reset() {
  // wipe the files in all subdirectories of both roots
  for (int i = 0; i < 2; i++) {
    const char *root = fs_root((al_storage_type_t)i);
    DIR *dir = opendir(root);
    if (dir == NULL) {
      continue;
    }
    for (;;) {
      struct dirent *entry = readdir(dir);
      if (entry == NULL) {
        break;
      }
      if (entry->d_type != DT_DIR || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        continue;
      }
      char sub[512];
      snprintf(sub, sizeof(sub), "%s/%s", root, entry->d_name);
      fs_wipe(sub);
    }
    closedir(dir);
  }
}
