/* SPDX-License-Identifier: MIT */
/* A pure C consumer: no engine headers or private SDK interfaces. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  qaplet_runtime *runtime = NULL;
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  const char source[] = "6 * 7";
  if (qaplet_create(&config, &runtime, NULL) != QAPLET_OK) return 1;
  if (qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) != QAPLET_OK) return 2;
  if (progress.phase != QAPLET_PHASE_DONE) return 3;
  if (qaplet_get_result(runtime, &result, NULL) != QAPLET_OK ||
      result.type != QAPLET_VALUE_DOUBLE || result.number != 42) return 4;
  if (qaplet_close(runtime, NULL) != QAPLET_OK) return 5;
  if (qaplet_destroy(runtime, NULL) != QAPLET_OK) return 6;
  puts("scalar: 42; closed: true");
  return 0;
}
