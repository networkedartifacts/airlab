#include "eng.h"
#include "eng_bundle.h"
#include "eng_exec.h"

bool eng_run() {
  // load bundle
  eng_bundle_t *bundle = eng_bundle_load();
  if (!bundle) {
    return false;
  }

  // execute bundle
  bool ok = eng_exec(bundle, "main");

  // free bundle
  eng_bundle_free(bundle);

  return ok;
}
