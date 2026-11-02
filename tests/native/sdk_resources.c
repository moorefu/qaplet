/* SPDX-License-Identifier: MIT */
/* Resource workloads use only the supplied public SDK. */
#include "qaplet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public resource contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static int release(qaplet_runtime *runtime) {
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  qaplet_snapshot closed = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &closed, NULL) == QAPLET_OK);
  CHECK(closed.lifecycle == QAPLET_CLOSED && !closed.engine_current_bytes &&
        !closed.queue_depth && !closed.module_count && !closed.source_cache_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

static int storage(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  config.limits.event_queue_slots = config.limits.event_payload_bytes = 1;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  /* Measure the public factory baseline, then allow a small real Script but
   * refuse the SDK's accepted source copy well below the source input limit. */
  config.limits.native_storage_bytes = snapshot.native_current_bytes + 2048;
  CHECK(!release(runtime));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_OK);
  char source[4096];
  memset(source, ' ', sizeof(source)); source[0] = '0';
  CHECK(qaplet_eval(runtime, source, sizeof(source), &progress, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.lifecycle == QAPLET_FAILED && snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
  CHECK(snapshot.native_peak_bytes <= config.limits.native_storage_bytes);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(!release(runtime));
  config.limits.native_storage_bytes = 0;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_OK);
  CHECK(!release(runtime));
  puts("resource storage: actual SDK source-copy refusal permanent; bounded accounting; fresh recovery: pass");
  return 0;
}

static int configuration(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(snapshot.limits.engine_heap_bytes == 67108864 && snapshot.limits.engine_stack_bytes == 1048576);
  CHECK(snapshot.limits.native_storage_bytes == 16777216 && snapshot.limits.source_bytes == 4194304);
  CHECK(snapshot.limits.source_cache_bytes == 33554432 && snapshot.limits.module_count == 256);
  CHECK(snapshot.limits.event_queue_slots == 256 && snapshot.limits.event_payload_bytes == 1024);
  CHECK(snapshot.limits.execution_timeout_ms == 30000 && snapshot.limits.execution_dispatches == 100000);
  CHECK(snapshot.limits.bridge_value_bytes == 1048576 && snapshot.limits.host_arguments == 32);
  CHECK(snapshot.limits.pump_jobs == 256 && snapshot.limits.pump_events == 32);
  CHECK(snapshot.engine_current_bytes && snapshot.engine_peak_bytes >= snapshot.engine_current_bytes);
  CHECK(snapshot.native_current_bytes && snapshot.native_peak_bytes >= snapshot.native_current_bytes);
  CHECK(!release(runtime));
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_RESOURCE_LIMITS));
  config.limits.engine_heap_bytes = 2097152; config.limits.engine_stack_bytes = 262144;
  config.limits.native_storage_bytes = 1048576; config.limits.source_bytes = 65536;
  config.limits.module_count = 8; config.limits.source_cache_bytes = 131072;
  config.limits.execution_timeout_ms = 3000; config.limits.execution_dispatches = 500;
  config.limits.event_queue_slots = 4; config.limits.event_payload_bytes = 8;
  config.limits.bridge_value_bytes = 4; config.limits.host_arguments = 40;
  config.limits.pump_jobs = 2; config.limits.pump_events = 2;
  qaplet_limits expected = config.limits;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  config = (qaplet_config)QAPLET_CONFIG_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(!memcmp(&snapshot.limits, &expected, sizeof(expected)));
  CHECK(!release(runtime));
#define REJECT_LIMIT(field) do { \
  config = (qaplet_config)QAPLET_CONFIG_INIT; config.limits.field = UINT64_MAX; \
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime); \
} while (0)
  REJECT_LIMIT(engine_stack_bytes); REJECT_LIMIT(source_bytes); REJECT_LIMIT(source_cache_bytes);
  REJECT_LIMIT(event_queue_slots); REJECT_LIMIT(event_payload_bytes); REJECT_LIMIT(execution_timeout_ms);
  REJECT_LIMIT(bridge_value_bytes); REJECT_LIMIT(host_arguments);
#undef REJECT_LIMIT
  puts("resource configuration: all finite defaults; copied fixed capacities; overflow rejection: pass");
  return 0;
}

static int settle(qaplet_runtime *runtime, qaplet_progress *progress) {
  for (unsigned i = 0; progress->phase == QAPLET_PHASE_PENDING && i < 2000; ++i)
    CHECK(qaplet_pump(runtime, progress, NULL) == QAPLET_OK);
  CHECK(progress->phase == QAPLET_PHASE_DONE);
  return 0;
}

static int number(qaplet_runtime *runtime, const char *source, double expected) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  CHECK(!settle(runtime, &progress));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK);
  CHECK(result.type == QAPLET_VALUE_DOUBLE && result.number == expected);
  return 0;
}

static qaplet_code echo(qaplet_runtime *runtime, uint64_t call, size_t argc,
                       const qaplet_value *argv, void *userdata) {
  unsigned *called = userdata;
  ++*called;
  qaplet_value result = QAPLET_VALUE_INIT;
  result.type = QAPLET_VALUE_DOUBLE; result.number = argc;
  return qaplet_host_set_result(runtime, call, argc == 1 ? &argv[0] : &result, NULL);
}

static int register_echo(qaplet_runtime *runtime, unsigned *called) {
  qaplet_host_export item = QAPLET_HOST_EXPORT_INIT;
  item.name = "echo"; item.name_length = 4; item.kind = QAPLET_HOST_FUNCTION;
  item.function = echo; item.userdata = called;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:resource"; module.name_length = 13;
  module.exports = &item; module.export_count = 1;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_OK);
  return 0;
}

static int bridge(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.bridge_value_bytes = 4; config.limits.host_arguments = 2;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  unsigned called = 0;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  uint64_t declared = snapshot.module_count;
  qaplet_host_export item = QAPLET_HOST_EXPORT_INIT;
  item.name = "data"; item.name_length = 4; item.kind = QAPLET_HOST_CONSTANT;
  item.value.type = QAPLET_VALUE_BYTES; item.value.data = "12345"; item.value.length = 5;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:oversize"; module.name_length = 13; module.exports = &item; module.export_count = 1;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_VALUE_TOO_LARGE);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == declared &&
        snapshot.lifecycle == QAPLET_OPEN && !snapshot.termination_reason);
  CHECK(!register_echo(runtime, &called));
  CHECK(!number(runtime, "import('host:resource').then(m=>{globalThis.echo=m.echo;return 42})", 42));
  CHECK(!number(runtime, "try{echo(1,2,3);0}catch(e){e.code}", QAPLET_VALUE_TOO_LARGE) && called == 0);
  CHECK(!number(runtime, "try{echo('12345');0}catch(e){e.code}", QAPLET_VALUE_TOO_LARGE) && called == 0);
  const char exact[] = "echo(new Uint8Array([65,0,66,67]))";
  CHECK(qaplet_eval(runtime, exact, sizeof(exact)-1, &progress, NULL) == QAPLET_OK && called == 1);
  unsigned char output[4]; result.buffer = output; result.capacity = sizeof(output);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BYTES &&
        result.length == 4 && !memcmp(output, "A\0BC", 4));
  CHECK(qaplet_eval(runtime, "'12345'", 7, &progress, NULL) == QAPLET_VALUE_TOO_LARGE);
  CHECK(qaplet_eval(runtime, "new Uint8Array(5)", 17, &progress, NULL) == QAPLET_VALUE_TOO_LARGE);
  CHECK(!number(runtime, "42", 42));
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_OPEN &&
        !snapshot.termination_reason && !snapshot.source_cache_bytes && snapshot.module_count == declared + 1);
  CHECK(!release(runtime));
  config.limits.host_arguments = 40;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(!register_echo(runtime, &called));
  CHECK(!number(runtime, "import('host:resource').then(m=>m.echo(...Array(33).fill(1)))", 33));
  CHECK(!release(runtime));
  puts("resource bridge: configured byte/argument limits; preflight reuse; copied exact boundary; larger finite arguments: pass");
  return 0;
}

static int slices(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.pump_jobs = 2; config.limits.pump_events = 2; config.limits.event_queue_slots = 4;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot before = QAPLET_SNAPSHOT_INIT, after = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char chain[] = "var p=Promise.resolve(0);for(let i=0;i<7;++i)p=p.then(n=>n+1);p";
  CHECK(qaplet_eval(runtime, chain, sizeof(chain)-1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
  CHECK(qaplet_get_snapshot(runtime, &after, NULL) == QAPLET_OK && after.execution_dispatches == 2);
  CHECK(!settle(runtime, &progress));
  CHECK(!number(runtime, "import('qaplet:events').then(m=>{globalThis.seen=0;m.onEvent(()=>{seen++});return 42})", 42));
  CHECK(qaplet_get_snapshot(runtime, &before, NULL) == QAPLET_OK);
  for (unsigned i = 0; i < 4; ++i) CHECK(qaplet_post_event(runtime, i, 0, NULL, 0, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 9, 0, NULL, 0, NULL) == QAPLET_QUEUE_FULL);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
  CHECK(qaplet_get_snapshot(runtime, &after, NULL) == QAPLET_OK);
  CHECK(after.queue_depth == 2 && after.queue_peak == 4 && after.events_consumed == before.events_consumed + 2 &&
        after.events_accepted == before.events_accepted + 4 && after.events_rejected == before.events_rejected + 1);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK);
  CHECK(!number(runtime, "seen", 4));
  CHECK(!release(runtime));
  puts("resource slices: configured jobs/events quotas; fixed queue; public event accounting: pass");
  return 0;
}

static int failed(qaplet_runtime *runtime) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_FAILED &&
        snapshot.termination_reason == QAPLET_MEMORY_LIMIT);
  CHECK(snapshot.engine_current_bytes <= snapshot.engine_peak_bytes && snapshot.engine_peak_bytes <= snapshot.limits.engine_heap_bytes);
  CHECK(snapshot.native_current_bytes <= snapshot.native_peak_bytes && snapshot.native_peak_bytes <= snapshot.limits.native_storage_bytes);
  CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_BAD_STATE);
  CHECK(!release(runtime));
  return 0;
}

static int refusals(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  config.limits.event_queue_slots = config.limits.event_payload_bytes = 1;
  qaplet_runtime *runtime = NULL;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  uint64_t baseline = snapshot.native_current_bytes;
  CHECK(!release(runtime));
  /* Genuine rejection tracking and copied registration storage, never private
   * allocator calls or a synthetic failure-injection seam. */
  config.limits.native_storage_bytes = baseline + 4096;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char rejections[] = "for(let i=0;i<1000;++i)Promise.reject(i);42";
  CHECK(qaplet_eval(runtime, rejections, sizeof(rejections)-1, &progress, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(!failed(runtime));
  config.limits.native_storage_bytes = baseline + 1024;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  unsigned char bytes[2048] = {0};
  qaplet_host_export item = QAPLET_HOST_EXPORT_INIT;
  item.name = "data"; item.name_length = 4; item.kind = QAPLET_HOST_CONSTANT;
  item.value.type = QAPLET_VALUE_BYTES; item.value.data = bytes; item.value.length = sizeof(bytes);
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name = "host:storage"; module.name_length = 12; module.exports = &item; module.export_count = 1;
  CHECK(qaplet_register_host_module(runtime, &module, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(!failed(runtime));
  config.limits.native_storage_bytes = baseline + 4096;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  unsigned called = 0;
  CHECK(!register_echo(runtime, &called));
  CHECK(!number(runtime, "import('host:resource').then(m=>{globalThis.echo=m.echo;return 42})", 42));
  const char caught_copy[] = "try{echo('x'.repeat(8192))}catch(e){};42";
  CHECK(qaplet_eval(runtime, caught_copy, sizeof(caught_copy)-1, &progress, NULL) == QAPLET_MEMORY_LIMIT && !called);
  CHECK(!failed(runtime));
  config.limits.native_storage_bytes = 0; config.limits.engine_heap_bytes = 1048576;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char caught[] = "try{let a=[];for(let i=0;i<100000;++i)a.push('x'.repeat(128))}catch(e){};42";
  CHECK(qaplet_eval(runtime, caught, sizeof(caught)-1, &progress, NULL) == QAPLET_MEMORY_LIMIT);
  CHECK(!failed(runtime));
  config.limits.engine_heap_bytes = 0;
  /* Include refusals before the first engine allocation and later factory
   * storage steps, with successful fresh creation after every refusal. */
  unsigned refusals = 0;
  for (unsigned i = 0; i < 32; ++i) {
    config.limits.native_storage_bytes = 1 + (baseline + 1024) * i / 31;
    qaplet_code code = qaplet_create(&config, &runtime, NULL);
    if (code) { CHECK(code == QAPLET_MEMORY_LIMIT && !runtime); ++refusals; }
    else CHECK(!release(runtime));
    qaplet_config recovery = QAPLET_CONFIG_INIT;
    CHECK(qaplet_create(&recovery, &runtime, NULL) == QAPLET_OK);
    CHECK(!number(runtime, "42", 42)); CHECK(!release(runtime));
  }
  CHECK(refusals > 0);
  puts("resource refusals: metered rejections/registration; caught bridge/engine refusal; 32 factory limits and recovery: pass");
  return 0;
}

int sdk_resources(const char *option) {
  if (!strcmp(option, "--resource-storage")) return storage();
  if (!strcmp(option, "--resource-configuration")) return configuration();
  if (!strcmp(option, "--resource-bridge")) return bridge();
  if (!strcmp(option, "--resource-slices")) return slices();
  if (!strcmp(option, "--resource-refusals")) return refusals();
  return 2;
}
