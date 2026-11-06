/* SPDX-License-Identifier: MIT */
/* Frozen M1 header from 5685bb6; deliberately never includes the new header. */
#include "m1-qaplet.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "M1 old-header contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static qaplet_code touch(qaplet_runtime *runtime, uint64_t call, size_t argc,
                         const qaplet_value *argv, void *opaque) {
  (void)runtime; (void)call; (void)argc; (void)argv;
  ++*(unsigned *)opaque;
  return QAPLET_OK;
}
static int settle(qaplet_runtime *runtime, qaplet_progress *progress) {
  for (unsigned i=0; progress->phase == QAPLET_PHASE_PENDING && i<1000; ++i)
    CHECK(qaplet_pump(runtime, progress, NULL) == QAPLET_OK);
  CHECK(progress->phase == QAPLET_PHASE_DONE);
  return 0;
}
static int evaluate(qaplet_runtime *runtime, const char *source) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  CHECK(!settle(runtime, &progress));
  return 0;
}
int sdk_m1_compat(const char *modules) {
  CHECK(QAPLET_ABI_VERSION == 1 && QAPLET_EXECUTION_STOPPED == 23 && QAPLET_HOST_ERROR == 19);
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & 98303) == 98303);
  config.reserved[0] = 1;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  config.reserved[0] = 0;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  unsigned called = 0;
  qaplet_host_export items[2] = {QAPLET_HOST_EXPORT_INIT, QAPLET_HOST_EXPORT_INIT};
  items[0].name = "answer"; items[0].name_length = 6; items[0].kind = QAPLET_HOST_CONSTANT;
  items[0].value.type = QAPLET_VALUE_DOUBLE; items[0].value.number = 42;
  items[1].name = "touch"; items[1].name_length = 5; items[1].kind = QAPLET_HOST_FUNCTION;
  items[1].function = touch; items[1].userdata = &called;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:old"; module.name_length = 8; module.exports = items; module.export_count = 2;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  CHECK(!evaluate(runtime, "import('host:old').then(m=>{globalThis.touch=m.touch;return m.answer})"));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(!evaluate(runtime, "new Uint8Array([0,255,42])"));
  unsigned char bytes[3] = {1,1,1}; result.buffer = bytes; result.capacity = sizeof(bytes);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BYTES && result.length == 3 && bytes[0] == 0 && bytes[1] == 255 && bytes[2] == 42);
  char path[4096]; CHECK(snprintf(path, sizeof(path), "%s/entry.mjs", modules) < (int)sizeof(path));
  CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_OK && !settle(runtime, &progress));
  CHECK(!evaluate(runtime, "moduleAnswer"));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  CHECK(!evaluate(runtime, "import('qaplet:events').then(m=>{globalThis.seen=0;m.onEvent(e=>{if(e.type===7&&e.token===9n&&e.payload[0]===42)seen=42})})"));
  unsigned char payload = 42;
  CHECK(qaplet_post_event(runtime, 7, 9, &payload, 1, NULL) == QAPLET_OK); payload = 0;
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
  CHECK(!evaluate(runtime, "seen"));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
  const char pending[] = "Promise.resolve().then(()=>touch());new Promise(()=>{})";
  CHECK(qaplet_eval(runtime, pending, sizeof(pending)-1, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && !called);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && !snapshot.engine_current_bytes);
  CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK && qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 7, 9, NULL, 0, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("M1 frozen header: new SDK link; values/modules/events; pending close; old ABI/errors/reserved: pass");
  return 0;
}
