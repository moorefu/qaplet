/* SPDX-License-Identifier: MIT */
/* This consumer uses only the supplied public SDK and real source files. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public module contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int sdk_modules(const char *directory) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  char path[4096];
  CHECK(snprintf(path, sizeof(path), "%s/entry.mjs", directory) < sizeof(path));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_OK);
  while (progress.phase == QAPLET_PHASE_PENDING)
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
  CHECK(progress.phase == QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_UNDEFINED);
  const char source[] = "moduleAnswer";
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("modules: real static graph; completed result: pass");
  return 0;
}
