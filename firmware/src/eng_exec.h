#ifndef ENG_EXEC_H
#define ENG_EXEC_H

void *eng_exec_start(eng_bundle_t *bundle, const char *binary);
void eng_exec_wait(void *ref);

#endif  // ENG_EXEC_H
