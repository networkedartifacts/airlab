#include <naos.h>
#include <naos/sys.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <al/core.h>
#include <al/storage.h>

#include "eng.h"
#include "eng_bundle.h"
#include "eng_exec.h"

#define ENG_DIR "engine"
#define ENG_LIST_SIZE 32
#define ENG_DEBUG false

static eng_plugin_t *eng_list = NULL;
static int eng_list_len = 0;

void eng_reload() {
  // ensure list
  if (eng_list == NULL) {
    eng_list = al_calloc(ENG_LIST_SIZE, sizeof(eng_plugin_t));
  }

  // open directory
  DIR *dir = opendir(AL_STORAGE_INTERNAL "/" ENG_DIR);
  if (dir == NULL && errno != ENOENT) {
    ESP_ERROR_CHECK(errno);
  }

  // stop if no directory
  if (dir == NULL) {
    return;
  }

  // clear list length
  eng_list_len = 0;

  // read directory
  for (;;) {
    // get entry
    struct dirent *entry = readdir(dir);
    if (entry == NULL) {
      break;
    }

    // log
    if (ENG_DEBUG) {
      naos_log("eng_reload: found '%s'", entry->d_name);
    }

    // ignore non regular files
    if (entry->d_type != DT_REG) {
      continue;
    }

    // handle specials
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    /* otherwise, handle files */

    // get list entry
    eng_plugin_t *info = &eng_list[eng_list_len];

    // copy file name
    strncpy(info->file, entry->d_name, sizeof(info->file));

    // set size
    info->size = al_storage_stat(AL_STORAGE_INT, ENG_DIR, info->file);

    // peek bundle
    eng_bundle_t *b = eng_bundle_load(info->file);
    if (!b) {
      naos_log("eng_reload: failed to peek bundle '%s'", info->file);
      continue;
    }

    // get attributes
    const char *name = eng_bundle_attr(b, "name", NULL);
    const char *title = eng_bundle_attr(b, "title", NULL);
    const char *version = eng_bundle_attr(b, "version", NULL);
    if (!name || !title || !version) {
      naos_log("eng_reload: missing bundle name/title/version");
      eng_bundle_free(b);
      continue;
    }

    // log
    if (ENG_DEBUG) {
      naos_log("eng_reload: got bundle name='%s' title='%s' version='%s'", name, title, version);
    }

    // copy attributes
    strncpy(info->name, name, sizeof(info->name));
    strncpy(info->title, title, sizeof(info->title));
    strncpy(info->version, version, sizeof(info->version));

    // free bundle
    eng_bundle_free(b);

    // increment
    eng_list_len++;
    if (eng_list_len >= ENG_LIST_SIZE) {
      naos_log("eng_reload: too many entries");
      break;
    }
  }

  // close directory
  closedir(dir);
}

size_t eng_num() {
  // return length
  return eng_list_len;
}

eng_plugin_t *eng_get(size_t index) {
  // check index
  if (index >= (size_t)eng_list_len) {
    return NULL;
  }

  return &eng_list[index];
}

bool eng_run(const char *file, const char *binary) {
  // run with no config
  return eng_run_config(file, binary, NULL);
}

bool eng_run_config(const char *file, const char *binary, eng_bundle_t *args) {
  // determine permissions
  eng_perm_t perms = 0;
  if (strcmp(binary, "main") == 0) {
    perms = ENG_PERM_ALL;
  } else if (strcmp(binary, "screen") == 0) {
    perms = ENG_PERM_GRAPHICS;
  }
  if (perms == 0) {
    return false;
  }

  // load bundle
  eng_bundle_t *bundle = eng_bundle_load(file);
  if (!bundle) {
    return false;
  }

  // get plugin name
  const char *name = eng_bundle_attr(bundle, "name", NULL);
  if (!name) {
    eng_bundle_free(bundle);
    return false;
  }

  // parse config schema from plugin bundle
  eng_bundle_t *config_schema = NULL;
  size_t config_schema_len = 0;
  void *defaults_data = eng_bundle_config(bundle, binary, &config_schema_len);
  if (defaults_data) {
    config_schema = eng_bundle_parse(defaults_data, config_schema_len);
  }

  // load stored config bundle (if not already set externally)
  eng_bundle_t *config_values = args;
  if (!config_values) {
    char values_file[96];
    snprintf(values_file, sizeof(values_file), "%s.alc", name);
    config_values = eng_bundle_load(values_file);
  }

  // start execution
  void *ref = eng_exec_start(bundle, binary, perms, config_schema, config_values);
  if (!ref) {
    if (config_values && config_values != args) {
      eng_bundle_free(config_values);
    }
    if (config_schema) {
      eng_bundle_free(config_schema);
    }
    eng_bundle_free(bundle);
    return false;
  }

  // wait for completion
  eng_exec_wait(ref);

  // free config
  if (config_values && config_values != args) {
    eng_bundle_free(config_values);
  }

  // free defaults
  if (config_schema) {
    eng_bundle_free(config_schema);
  }

  // free bundle
  eng_bundle_free(bundle);

  return true;
}
