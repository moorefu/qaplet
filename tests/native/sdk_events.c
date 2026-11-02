/* SPDX-License-Identifier: MIT */
/* Public-header-only event contract; this source ships in the SDK. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public event contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static int settle(qaplet_runtime *runtime, qaplet_progress *progress) {
  for (unsigned i = 0; progress->phase == QAPLET_PHASE_PENDING && i < 1000; ++i)
    CHECK(qaplet_pump(runtime, progress, NULL) == QAPLET_OK);
  CHECK(progress->phase == QAPLET_PHASE_DONE);
  return 0;
}

static int check_js(qaplet_runtime *runtime, const char *source) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  CHECK(!settle(runtime, &progress));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BOOL && result.boolean);
  return 0;
}

static int release(qaplet_runtime *runtime) {
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

static int copy_values(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi) == QAPLET_OK && (abi.capabilities & QAPLET_CAP_POD_EVENTS));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 1, 0, NULL, 0, NULL) == QAPLET_NO_HANDLER);
  const char source[] = "globalThis.received=[];import('qaplet:events').then(m=>{m.onEvent(e=>{received.push(e)});try{m.onEvent(()=>{})}catch(e){return e instanceof Error}return false})";
  CHECK(qaplet_eval(runtime, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK);
  CHECK(!settle(runtime, &progress));
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BOOL && result.boolean);
  unsigned char data[] = {0,255,17};
  CHECK(qaplet_post_event(runtime, UINT32_MAX, UINT64_MAX, data, sizeof(data), NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 0, 0, data, sizeof(data), NULL) == QAPLET_OK);
  memset(data, 99, sizeof(data));
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed && progress.phase == QAPLET_PHASE_DONE);
  const char check[] = "received.length===2 && received[0].type===4294967295 && received[0].token===18446744073709551615n && received[1].type===0 && received[1].token===0n && received.every(e=>e.payload instanceof Uint8Array && e.payload.join(',')==='0,255,17') && received[0].payload!==received[1].payload && (received[0].payload[0]=42,received[1].payload[0]===0)";
  CHECK(qaplet_eval(runtime, check, sizeof(check)-1, &progress, NULL) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BOOL && result.boolean);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  puts("events: once-only registration; copied POD and independent exact JS values: pass");
  return 0;
}

static qaplet_code note(qaplet_runtime *runtime, uint64_t call, size_t argc,
                         const qaplet_value *argv, void *opaque) {
  (void)runtime; (void)call; (void)argc; (void)argv;
  ++*(unsigned *)opaque;
  return QAPLET_OK;
}

static qaplet_code oversized_post(qaplet_runtime *runtime, uint64_t call, size_t argc,
                                   const qaplet_value *argv, void *opaque) {
  (void)call; (void)argc; (void)argv; (void)opaque;
  unsigned char data[4]={0};
  return qaplet_post_event(runtime,0,0,data,sizeof(data),NULL);
}

static int queue(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  unsigned char data[1025] = {0};
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == 1 &&
        snapshot.limits.event_queue_slots == 256 && snapshot.limits.event_payload_bytes == 1024);
  CHECK(qaplet_post_event(runtime, 0, 0, NULL, 1, NULL) == QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_post_event(runtime, 0, 0, data, 1025, NULL) == QAPLET_PAYLOAD_TOO_LARGE);
  CHECK(qaplet_post_event(runtime, 0, 0, NULL, 0, NULL) == QAPLET_NO_HANDLER);
  /* A valid idle pump locks C declarations, independently of the JS window. */
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
  qaplet_host_module late = QAPLET_HOST_MODULE_INIT;
  late.name = "host:late"; late.name_length = 9;
  CHECK(qaplet_register_host_module(runtime, &late, NULL) == QAPLET_BAD_STATE);
  CHECK(!check_js(runtime, "globalThis.received=[];import('qaplet:events').then(m=>{for(let v of [null,undefined,42,{}]){try{m.onEvent(v);return false}catch(e){}}m.onEvent(e=>received.push(e));return true})"));
  CHECK(!check_js(runtime, "true"));
  for (unsigned i = 0; i < 256; ++i) {
    data[0] = i; data[1023] = 255;
    CHECK(qaplet_post_event(runtime, i, i, data, 1024, NULL) == QAPLET_OK);
  }
  memset(data, 99, sizeof(data));
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT; char text[32];
  diagnostic.message = text; diagnostic.capacity = sizeof(text);
  CHECK(qaplet_post_event(runtime, 999, 999, NULL, 0, &diagnostic) == QAPLET_QUEUE_FULL && !strcmp(text,"QUEUE_FULL"));
  CHECK(qaplet_post_event(runtime, 999, 999, data, 1025, &diagnostic) == QAPLET_PAYLOAD_TOO_LARGE && !strcmp(text,"PAYLOAD_TOO_LARGE"));
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BOOL && result.boolean);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.queue_depth == 256 && snapshot.queue_peak == 256 && snapshot.events_accepted == 256 && snapshot.events_rejected == 5 && !snapshot.events_consumed);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.queue_depth == 224 && snapshot.events_consumed == 32);
  memset(data,0,sizeof(data));
  for (unsigned i = 256; i < 288; ++i) {
    data[0] = i; data[1023] = 255;
    CHECK(qaplet_post_event(runtime, i, i, data, 1024, NULL) == QAPLET_OK);
  }
  memset(data,99,sizeof(data));
  CHECK(qaplet_post_event(runtime, 999, 0, NULL, 0, NULL) == QAPLET_QUEUE_FULL);
  for (unsigned i = 0; i < 8; ++i) CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && !snapshot.queue_depth && snapshot.events_consumed == 288 && snapshot.events_accepted == 288 && snapshot.events_rejected == 6);
  CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && !progress.progressed);
  CHECK(!check_js(runtime,"received.length===288 && received.every((e,i)=>e.type===i && e.token===BigInt(i) && e.payload.length===1024 && e.payload[0]===(i%256) && e.payload[1]===0 && e.payload[1023]===255)"));
  CHECK(!release(runtime));

  config.limits.event_queue_slots = 2; config.limits.event_payload_bytes = 3;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  unsigned close_calls=0;
  qaplet_host_export marks[]={QAPLET_HOST_EXPORT_INIT,QAPLET_HOST_EXPORT_INIT};
  marks[0].name="mark"; marks[0].name_length=4; marks[0].kind=QAPLET_HOST_FUNCTION; marks[0].function=note; marks[0].userdata=&close_calls;
  marks[1].name="refuse"; marks[1].name_length=6; marks[1].kind=QAPLET_HOST_FUNCTION; marks[1].function=oversized_post;
  qaplet_host_module close_module=QAPLET_HOST_MODULE_INIT;
  close_module.name="host:close"; close_module.name_length=10; close_module.exports=marks; close_module.export_count=2;
  CHECK(qaplet_register_host_module(runtime,&close_module,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"Promise.all([import('qaplet:events'),import('host:close')]).then(([m,h])=>{try{h.refuse();return false}catch(e){if(e.code!==22)return false}m.onEvent(()=>h.mark());globalThis.closeWait=new Promise(()=>{}).finally(()=>h.mark());return true})"));
  CHECK(qaplet_post_event(runtime, 1, 0, data, 3, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 2, 0, NULL, 0, NULL) == QAPLET_OK);
  CHECK(qaplet_post_event(runtime, 3, 0, data, 4, NULL) == QAPLET_PAYLOAD_TOO_LARGE);
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(!close_calls);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.lifecycle == QAPLET_CLOSED && !snapshot.queue_depth && !snapshot.events_consumed && snapshot.events_dropped == 2 && !snapshot.engine_current_bytes);
  CHECK(qaplet_post_event(runtime, 0, 0, NULL, 0, NULL) == QAPLET_BAD_STATE);
  CHECK(qaplet_post_event(runtime, 0, 0, data, 4, NULL) == QAPLET_PAYLOAD_TOO_LARGE);
  CHECK(qaplet_post_event(runtime, 0, 0, NULL, 1, NULL) == QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  config = (qaplet_config)QAPLET_CONFIG_INIT; config.limits.module_count = 1;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_register_host_module(runtime, &late, NULL) == QAPLET_MODULE_LIMIT);
  CHECK(!check_js(runtime,"import('qaplet:events').then(m=>typeof m.onEvent==='function')"));
  CHECK(!release(runtime));
  config = (qaplet_config)QAPLET_CONFIG_INIT; config.limits.event_queue_slots = UINT64_MAX;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  config.limits.event_queue_slots = 2; config.limits.event_payload_bytes = UINT64_MAX;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_INVALID_ARGUMENT && !runtime);
  puts("event queue: finite defaults; full/oversize atomic refusal; FIFO wrap; raw close drop: pass");
  return 0;
}

static int scheduling(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  const char source[] = "globalThis.order=[];globalThis.jobs=0;globalThis.side=0;globalThis.hooks=0;import('qaplet:events').then(m=>{m.onEvent(e=>{order.push(e.type);if(e.type===1 || e.type===10){let p=Promise.resolve();for(let i=0;i<600;++i)p=p.then(()=>{jobs++});if(e.type===10){let bad=Promise.reject(new Error('handled at checkpoint'));p=p.then(()=>bad.catch(()=>{}))}return p}if(e.type===2)return new Promise(()=>{});if(e.type===3)return {get then(){hooks++;throw 1}};if(e.type===11)return Promise.reject(new Error('returned rejection'));if(e.type===13){Promise.resolve().then(()=>side++);throw new Error('event throw')}});return true})";
  CHECK(!check_js(runtime, source));
  for (unsigned i=1;i<=3;++i) CHECK(qaplet_post_event(runtime,i,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING && progress.progressed);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.events_consumed==1 && snapshot.queue_depth==2 && snapshot.execution_dispatches==257);
  CHECK(qaplet_eval(runtime,"true",4,&progress,NULL)==QAPLET_BUSY);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.events_consumed==1 && snapshot.execution_dispatches==513);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_DONE);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.events_consumed==3 && !snapshot.queue_depth && snapshot.execution_dispatches==1);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_UNDEFINED);
  CHECK(!check_js(runtime,"order.join(',')==='1,2,3' && jobs===600 && hooks===0"));
  CHECK(qaplet_post_event(runtime,10,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_DONE);
  CHECK(qaplet_post_event(runtime,11,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,12,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_JS_EXCEPTION && progress.phase==QAPLET_PHASE_DONE);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && snapshot.queue_depth==1 && snapshot.events_consumed==5);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_JS_EXCEPTION);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_DONE);
  CHECK(qaplet_post_event(runtime,13,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,14,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_JS_EXCEPTION && progress.phase==QAPLET_PHASE_DONE);
  CHECK(!check_js(runtime,"side===1 && order.join(',')==='1,2,3,10,11,12,13'"));
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"side===1 && order.join(',')==='1,2,3,10,11,12,13,14'"));
  CHECK(!release(runtime));
  puts("event scheduling: jobs-first slices; ignored Promise return; checkpoint rejection; ordinary-error reuse: pass");
  return 0;
}

typedef struct {
  qaplet_runtime *runtime;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned turn;
} producers;
typedef struct { producers *shared; unsigned id; int failed; } producer;

static void *produce(void *opaque) {
  producer *self = opaque;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  self->failed = qaplet_pump(self->shared->runtime,&progress,NULL)!=QAPLET_WRONG_THREAD ||
      qaplet_close(self->shared->runtime,NULL)!=QAPLET_WRONG_THREAD;
  for (unsigned i=0;i<32;++i) {
    pthread_mutex_lock(&self->shared->mutex);
    while (self->shared->turn != 2*i+self->id)
      pthread_cond_wait(&self->shared->condition,&self->shared->mutex);
    unsigned char bytes[] = {self->id,i};
    if (qaplet_post_event(self->shared->runtime,1,self->shared->turn,bytes,sizeof(bytes),NULL)!=QAPLET_OK) self->failed=1;
    memset(bytes,99,sizeof(bytes));
    ++self->shared->turn;
    pthread_cond_broadcast(&self->shared->condition);
    pthread_mutex_unlock(&self->shared->mutex);
  }
  return NULL;
}

typedef struct {
  qaplet_runtime *runtime;
  pthread_t owner;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int entered, posted, failed;
} owner_gate;

static qaplet_code gate_callback(qaplet_runtime *runtime, uint64_t call,
                                 size_t argc, const qaplet_value *argv, void *opaque) {
  (void)call; (void)argc; (void)argv;
  owner_gate *gate = opaque;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  if (!pthread_equal(gate->owner,pthread_self()) ||
      qaplet_pump(runtime,&progress,NULL)!=QAPLET_BUSY ||
      qaplet_eval(runtime,"true",4,&progress,NULL)!=QAPLET_BUSY ||
      qaplet_get_result(runtime,&result,NULL)!=QAPLET_BUSY ||
      qaplet_close(runtime,NULL)!=QAPLET_BUSY ||
      qaplet_get_snapshot(runtime,&snapshot,NULL)!=QAPLET_OK || snapshot.phase!=QAPLET_PHASE_RUNNING)
    gate->failed=1;
  pthread_mutex_lock(&gate->mutex);
  gate->entered=1;
  pthread_cond_broadcast(&gate->condition);
  while (!gate->posted) pthread_cond_wait(&gate->condition,&gate->mutex);
  pthread_mutex_unlock(&gate->mutex);
  if (qaplet_post_event(runtime,102,0,NULL,0,NULL)!=QAPLET_OK) gate->failed=1;
  return QAPLET_OK;
}

static void *post_during_callback(void *opaque) {
  owner_gate *gate = opaque;
  pthread_mutex_lock(&gate->mutex);
  while (!gate->entered) pthread_cond_wait(&gate->condition,&gate->mutex);
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  if (qaplet_post_event(gate->runtime,101,0,NULL,0,NULL)!=QAPLET_OK ||
      qaplet_get_snapshot(gate->runtime,&snapshot,NULL)!=QAPLET_OK || snapshot.phase!=QAPLET_PHASE_RUNNING)
    gate->failed=1;
  gate->posted=1;
  pthread_cond_broadcast(&gate->condition);
  pthread_mutex_unlock(&gate->mutex);
  return NULL;
}

static int threads(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"globalThis.received=[];import('qaplet:events').then(m=>{m.onEvent(e=>received.push(e));return true})"));
  producers shared = {.runtime=runtime};
  CHECK(!pthread_mutex_init(&shared.mutex,NULL) && !pthread_cond_init(&shared.condition,NULL));
  producer a = {&shared,0,0}, b = {&shared,1,0}; pthread_t one,two;
  CHECK(!pthread_create(&one,NULL,produce,&a) && !pthread_create(&two,NULL,produce,&b));
  CHECK(!pthread_join(one,NULL) && !pthread_join(two,NULL) && !a.failed && !b.failed && shared.turn==64);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"received.length===64 && received.every((e,i)=>e.token===BigInt(i) && e.payload.join(',')===[i%2,Math.floor(i/2)].join(','))"));
  CHECK(!release(runtime));
  CHECK(!pthread_cond_destroy(&shared.condition) && !pthread_mutex_destroy(&shared.mutex));

  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  owner_gate gate = {.runtime=runtime,.owner=pthread_self()};
  CHECK(!pthread_mutex_init(&gate.mutex,NULL) && !pthread_cond_init(&gate.condition,NULL));
  qaplet_host_export item = QAPLET_HOST_EXPORT_INIT;
  item.name="gate"; item.name_length=4; item.kind=QAPLET_HOST_FUNCTION; item.function=gate_callback; item.userdata=&gate;
  qaplet_host_module module = QAPLET_HOST_MODULE_INIT;
  module.name="host:events"; module.name_length=11; module.exports=&item; module.export_count=1;
  CHECK(qaplet_register_host_module(runtime,&module,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"globalThis.received=[];Promise.all([import('qaplet:events'),import('host:events')]).then(([m,h])=>{m.onEvent(e=>{received.push(e.type);if(e.type===100)h.gate()});return true})"));
  CHECK(!pthread_create(&one,NULL,post_during_callback,&gate));
  CHECK(qaplet_post_event(runtime,100,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(!pthread_join(one,NULL) && !gate.failed);
  CHECK(!check_js(runtime,"received.join(',')==='100,101,102'"));
  CHECK(!release(runtime));
  CHECK(!pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
  puts("event threads: controlled producer FIFO; owner/callback barriers; post and snapshot reentry: pass");
  return 0;
}

static int terminal(qaplet_runtime *runtime, qaplet_code reason) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_FAILED && snapshot.termination_reason==reason && snapshot.phase==QAPLET_PHASE_DONE);
  CHECK(qaplet_get_result(runtime,&result,NULL)==reason && result.code==reason);
  CHECK(qaplet_post_event(runtime,0,0,NULL,0,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_eval(runtime,"true",4,&progress,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_CLOSED && snapshot.termination_reason==reason && snapshot.events_dropped==1 && !snapshot.engine_current_bytes && !snapshot.queue_depth);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK);
  return 0;
}

static int budgets(void) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  const char handler[] = "import('qaplet:events').then(m=>{m.onEvent(()=>{let p=Promise.resolve();for(let i=0;i<600;++i)p=p.then(()=>{});return p});return true})";
  config.limits.execution_dispatches=300;
  for (unsigned active=0;active<2;++active) {
    CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
    CHECK(!check_js(runtime,handler));
    if (active) {
      const char top[] = "var p=Promise.resolve();for(let i=0;i<128;++i)p=p.then(()=>{});new Promise(()=>{})";
      CHECK(qaplet_eval(runtime,top,sizeof(top)-1,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
    }
    CHECK(qaplet_post_event(runtime,1,0,NULL,0,NULL)==QAPLET_OK);
    CHECK(qaplet_post_event(runtime,2,0,NULL,0,NULL)==QAPLET_OK);
    CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
    CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.execution_dispatches==257 && snapshot.events_consumed==1 && snapshot.queue_depth==1);
    CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_DISPATCH_LIMIT && progress.phase==QAPLET_PHASE_DONE);
    CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.execution_dispatches==300 && snapshot.events_consumed==1);
    CHECK(!terminal(runtime,QAPLET_DISPATCH_LIMIT));
  }
  config=(qaplet_config)QAPLET_CONFIG_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"import('qaplet:events').then(m=>{m.onEvent(()=>{});return true})"));
  qaplet_execution_options options=QAPLET_EXECUTION_OPTIONS_INIT; options.max_dispatches=32;
  CHECK(qaplet_eval_with_options(runtime,"new Promise(()=>{})",19,&options,&progress,NULL)==QAPLET_OK);
  for (unsigned i=0;i<33;++i) CHECK(qaplet_post_event(runtime,i,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING && progress.progressed);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_DISPATCH_LIMIT && !progress.progressed);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.events_consumed==32 && snapshot.execution_dispatches==32);
  CHECK(!terminal(runtime,QAPLET_DISPATCH_LIMIT));
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.limits.execution_timeout_ms=100;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,handler));
  CHECK(qaplet_post_event(runtime,1,0,NULL,0,NULL)==QAPLET_OK && qaplet_post_event(runtime,2,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  /* Intentional owner handoff beyond the event ledger's absolute deadline. */
  struct timespec before,now;
  CHECK(!clock_gettime(CLOCK_MONOTONIC,&before));
  do {
    struct timespec pause={0,10000000};
    CHECK(!nanosleep(&pause,NULL) || errno==EINTR);
    CHECK(!clock_gettime(CLOCK_MONOTONIC,&now));
  } while ((now.tv_sec-before.tv_sec)*1000000000LL+now.tv_nsec-before.tv_nsec<150000000LL);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_DEADLINE_EXCEEDED && !progress.progressed);
  CHECK(!terminal(runtime,QAPLET_DEADLINE_EXCEEDED));
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.limits.engine_heap_bytes=1048576;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(!check_js(runtime,"import('qaplet:events').then(m=>{m.onEvent(()=>{try{let data=[];for(;;)data.push('x'.repeat(16384))}catch(e){};for(;;){}});return true})"));
  CHECK(qaplet_post_event(runtime,1,0,NULL,0,NULL)==QAPLET_OK && qaplet_post_event(runtime,2,0,NULL,0,NULL)==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_MEMORY_LIMIT);
  CHECK(!terminal(runtime,QAPLET_MEMORY_LIMIT));
  config=(qaplet_config)QAPLET_CONFIG_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK);
  config.limits.native_storage_bytes=snapshot.native_current_bytes-1;
  CHECK(!release(runtime));
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_MEMORY_LIMIT && !runtime);
  puts("event budgets: shared/idle cumulative ledgers; owner-wait deadline; allocation first cause; factory refusal: pass");
  return 0;
}

typedef struct { qaplet_runtime *runtime; qaplet_code code; } entry_post;
static void *post_entry(void *opaque) {
  entry_post *post=opaque;
  unsigned char data[]={0,255,17};
  post->code=qaplet_post_event(post->runtime,42,UINT64_MAX,data,sizeof(data),NULL);
  memset(data,99,sizeof(data));
  return NULL;
}

int sdk_events_entry(const char *directory) {
  qaplet_config config=QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  char path[4096]; CHECK(snprintf(path,sizeof(path),"%s/events-entry.mjs",directory)<sizeof(path));
  CHECK(qaplet_run_entry(runtime,path,strlen(path),&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING && !progress.progressed);
  CHECK(qaplet_eval(runtime,"true",4,&progress,NULL)==QAPLET_BUSY);
  entry_post post={runtime,QAPLET_BAD_STATE}; pthread_t worker;
  CHECK(!pthread_create(&worker,NULL,post_entry,&post) && !pthread_join(worker,NULL) && post.code==QAPLET_OK);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(!settle(runtime,&progress));
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_UNDEFINED);
  CHECK(!check_js(runtime,"entryEventResult===42 && entryEvents.length===1 && entryEvents[0].token===18446744073709551615n && entryEvents[0].payload.join(',')==='0,255,17'"));
  CHECK(!release(runtime));
  puts("event entry: static builtin; no-progress TLA; worker copy; owner pump completion: pass");
  return 0;
}

int sdk_events(const char *mode) {
  if (!strcmp(mode,"--events")) return copy_values();
  if (!strcmp(mode,"--events-queue")) return queue();
  if (!strcmp(mode,"--events-scheduling")) return scheduling();
  if (!strcmp(mode,"--events-threads")) return threads();
  if (!strcmp(mode,"--events-budgets")) return budgets();
  return 2;
}
