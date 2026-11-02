/* SPDX-License-Identifier: MIT */
/* Budget behavior is observed only through the distributed public SDK. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>

static int deadline(void);

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public budget contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

int sdk_budgets(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.limits.execution_timeout_ms == 30000);
  CHECK(snapshot.limits.execution_dispatches == 100000);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  CHECK(!deadline());
  puts("budgets: finite defaults; monotonic loop interruption: pass");
  return 0;
}

static int failed(qaplet_runtime *runtime, qaplet_code reason) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_FAILED && snapshot.phase == QAPLET_PHASE_DONE);
  CHECK(snapshot.termination_reason == reason);
  CHECK(qaplet_get_result(runtime, &result, NULL) == reason && result.code == reason);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_CLOSED && snapshot.termination_reason == reason &&
        snapshot.engine_current_bytes == 0);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

static int deadline(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.execution_timeout_ms = 10;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  /* Finite even without the product interrupt, so red is a returned mismatch. */
  const char source[] = "var end=Date.now()+100; while(Date.now()<end){}; 42";
  CHECK(qaplet_eval(runtime, source, sizeof(source) - 1, &progress, NULL) == QAPLET_DEADLINE_EXCEEDED);
  CHECK(!failed(runtime, QAPLET_DEADLINE_EXCEEDED));
  return 0;
}
