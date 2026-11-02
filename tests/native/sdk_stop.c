/* SPDX-License-Identifier: MIT */
/* Public-header-only cooperative stop contract, distributed with the SDK. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define CHECK(condition) do { if (!(condition)) { \
  fprintf(stderr, "public stop contract failed at line %d: %s\n", __LINE__, #condition); return 1; \
} } while (0)

static int settle(qaplet_runtime *runtime, const char *source) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
  for (unsigned i=0; progress.phase==QAPLET_PHASE_PENDING && i<1000; ++i)
    CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(progress.phase==QAPLET_PHASE_DONE);
  return 0;
}

static qaplet_code mark(qaplet_runtime *runtime, uint64_t call, size_t argc,
                         const qaplet_value *argv, void *opaque) {
  (void)runtime; (void)call; (void)argc; (void)argv;
  ++*(unsigned *)opaque;
  return QAPLET_OK;
}

static int register_mark(qaplet_runtime *runtime, unsigned *marks) {
  qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;
  item.name="mark"; item.name_length=4; item.kind=QAPLET_HOST_FUNCTION; item.function=mark; item.userdata=marks;
  qaplet_host_module module=QAPLET_HOST_MODULE_INIT;
  module.name="host:stop"; module.name_length=9; module.exports=&item; module.export_count=1;
  CHECK(qaplet_register_host_module(runtime,&module,NULL)==QAPLET_OK);
  return 0;
}

static int stopped(qaplet_runtime *runtime, uint64_t dropped) {
  qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_STOPPED &&
        snapshot.phase==QAPLET_PHASE_DONE && snapshot.termination_reason==QAPLET_EXECUTION_STOPPED);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_EXECUTION_STOPPED);
  uint64_t dispatches=snapshot.execution_dispatches, consumed=snapshot.events_consumed;
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK && qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(runtime,"42",2,&progress,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_CLOSED &&
        snapshot.termination_reason==QAPLET_EXECUTION_STOPPED && !snapshot.engine_current_bytes &&
        !snapshot.queue_depth && snapshot.events_dropped==dropped &&
        snapshot.execution_dispatches==dispatches && snapshot.events_consumed==consumed);
  uint64_t native=snapshot.native_current_bytes;
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK && qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_CLOSED &&
        snapshot.native_current_bytes==native && snapshot.termination_reason==QAPLET_EXECUTION_STOPPED);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK);
  return 0;
}

static int idle_and_queue(void) {
  qaplet_abi abi = QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK && (abi.capabilities & QAPLET_CAP_COOPERATIVE_STOP));
  CHECK(qaplet_request_stop(NULL,NULL)==QAPLET_INVALID_ARGUMENT);
  qaplet_config config=QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  qaplet_diagnostic invalid=QAPLET_DIAGNOSTIC_INIT; invalid.struct_size=8;
  CHECK(qaplet_request_stop(runtime,&invalid)==QAPLET_ABI_MISMATCH);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_STOP_REQUESTED && snapshot.phase==QAPLET_PHASE_IDLE);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_EXECUTION_STOPPED && !progress.progressed);
  CHECK(!stopped(runtime,0));

  config.limits.event_queue_slots=2; config.limits.event_payload_bytes=1;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  unsigned marks=0;
  CHECK(!register_mark(runtime,&marks));
  CHECK(!settle(runtime,"Promise.all([import('qaplet:events'),import('host:stop')]).then(([m,h])=>{globalThis.h=h;m.onEvent(()=>h.mark());globalThis.wait=new Promise(()=>{}).finally(()=>h.mark())})"));
  unsigned char bytes=42;
  CHECK(qaplet_post_event(runtime,1,0,&bytes,1,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,2,0,&bytes,1,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,3,0,NULL,0,NULL)==QAPLET_QUEUE_FULL);
  const char source[]="Promise.resolve().then(()=>h.mark()).finally(()=>h.mark());new Promise(()=>{})";
  CHECK(qaplet_eval(runtime,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(runtime,3,0,NULL,0,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_post_event(runtime,3,0,NULL,1,NULL)==QAPLET_INVALID_ARGUMENT);
  CHECK(qaplet_post_event(runtime,3,0,&bytes,2,NULL)==QAPLET_PAYLOAD_TOO_LARGE);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_STOP_REQUESTED &&
        snapshot.queue_depth==2 && snapshot.events_accepted==2 && !snapshot.events_consumed && snapshot.events_rejected==4);
  char text[8]; qaplet_diagnostic diagnostic=QAPLET_DIAGNOSTIC_INIT;
  diagnostic.message=text; diagnostic.capacity=sizeof(text);
  CHECK(qaplet_pump(runtime,&progress,&diagnostic)==QAPLET_EXECUTION_STOPPED && !progress.progressed &&
        diagnostic.code==QAPLET_EXECUTION_STOPPED && diagnostic.flags==QAPLET_DIAGNOSTIC_TRUNCATED && text[7]==0);
  CHECK(!marks);
  CHECK(!stopped(runtime,2));
  CHECK(!marks);
  puts("stop: queue-independent acceptance; immediate refusal; owner confirmation; raw jobs/events close: pass");
  return 0;
}

typedef struct {
  qaplet_runtime *runtime;
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  int entered, released, failed, mode, fill_queue;
  unsigned arms, marks;
  uint64_t owner_cpu_ns;
  uint64_t callback_hold_ns;
  qaplet_snapshot accepted;
} stop_gate;

static uint64_t nanoseconds(clockid_t clock) {
  struct timespec time;
  if (clock_gettime(clock,&time)) return 0;
  return (uint64_t)time.tv_sec*1000000000+(uint64_t)time.tv_nsec;
}

/* Only this worker and the owner exist. Subtract this worker's thread CPU from
 * process CPU to observe owner progress after the JS arm callback has returned.
 * The loop allocates nothing and invokes no C functions after arm. Sleep merely
 * yields the polling worker; elapsed wall time never proves JS started/stopped. */
static uint64_t other_cpu(void) {
  uint64_t process=nanoseconds(CLOCK_PROCESS_CPUTIME_ID);
  uint64_t self=nanoseconds(CLOCK_THREAD_CPUTIME_ID);
  return process>self ? process-self : 0;
}

static qaplet_code arm(qaplet_runtime *runtime, uint64_t call, size_t argc,
                        const qaplet_value *argv, void *opaque) {
  (void)argc; (void)argv;
  stop_gate *gate=opaque;
  ++gate->arms;
  pthread_mutex_lock(&gate->mutex);
  gate->entered=1;
  pthread_cond_broadcast(&gate->condition);
  if (!gate->mode) while (!gate->released) pthread_cond_wait(&gate->condition,&gate->mutex);
  pthread_mutex_unlock(&gate->mutex);
  if (!gate->mode) {
    if (gate->callback_hold_ns) {
      uint64_t until=nanoseconds(CLOCK_MONOTONIC)+gate->callback_hold_ns;
      while (nanoseconds(CLOCK_MONOTONIC)<until) {
        struct timespec yield={0,1000000}; nanosleep(&yield,NULL);
      }
    }
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
    qaplet_value value=QAPLET_VALUE_INIT; value.type=QAPLET_VALUE_BOOL; value.boolean=1;
    if (qaplet_pump(runtime,&progress,NULL)!=QAPLET_BUSY || qaplet_close(runtime,NULL)!=QAPLET_BUSY ||
        qaplet_request_stop(runtime,NULL)!=QAPLET_OK ||
        qaplet_get_snapshot(runtime,&snapshot,NULL)!=QAPLET_OK || snapshot.lifecycle!=QAPLET_STOP_REQUESTED ||
        qaplet_host_set_result(runtime,call,&value,NULL)!=QAPLET_OK) gate->failed=1;
  }
  return QAPLET_OK;
}

static void *stop_worker(void *opaque) {
  stop_gate *gate=opaque;
  pthread_mutex_lock(&gate->mutex);
  while (!gate->entered) pthread_cond_wait(&gate->condition,&gate->mutex);
  pthread_mutex_unlock(&gate->mutex);
  if (gate->mode==1) {
    uint64_t start=other_cpu(), until=nanoseconds(CLOCK_MONOTONIC)+2000000000;
    if (!start) gate->failed=1;
    do {
      uint64_t current=other_cpu();
      if (current>=start) gate->owner_cpu_ns=current-start;
      if (gate->owner_cpu_ns>=20000000) break;
      struct timespec yield={0,1000000}; nanosleep(&yield,NULL);
    } while (nanoseconds(CLOCK_MONOTONIC)<until);
    if (gate->owner_cpu_ns<20000000) gate->failed=1;
  } else if (gate->mode==2) {
    uint64_t until=nanoseconds(CLOCK_MONOTONIC)+2000000000;
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
    do {
      if (qaplet_get_snapshot(gate->runtime,&snapshot,NULL)!=QAPLET_OK) gate->failed=1;
      if (snapshot.lifecycle==QAPLET_FAILED) break;
      struct timespec yield={0,1000000}; nanosleep(&yield,NULL);
    } while (nanoseconds(CLOCK_MONOTONIC)<until);
    if (snapshot.lifecycle!=QAPLET_FAILED || snapshot.termination_reason!=QAPLET_DEADLINE_EXCEEDED) gate->failed=1;
  }
  if (gate->fill_queue) {
    if (qaplet_post_event(gate->runtime,2,0,NULL,0,NULL)!=QAPLET_OK ||
        qaplet_post_event(gate->runtime,3,0,NULL,0,NULL)!=QAPLET_OK ||
        qaplet_post_event(gate->runtime,4,0,NULL,0,NULL)!=QAPLET_QUEUE_FULL) gate->failed=1;
  }
  gate->accepted=(qaplet_snapshot)QAPLET_SNAPSHOT_INIT;
  if (qaplet_request_stop(gate->runtime,NULL)!=QAPLET_OK ||
      qaplet_request_stop(gate->runtime,NULL)!=QAPLET_OK ||
      qaplet_get_snapshot(gate->runtime,&gate->accepted,NULL)!=QAPLET_OK ||
      qaplet_post_event(gate->runtime,4,0,NULL,0,NULL)!=QAPLET_BAD_STATE) gate->failed=1;
  pthread_mutex_lock(&gate->mutex);
  gate->released=1;
  pthread_cond_broadcast(&gate->condition);
  pthread_mutex_unlock(&gate->mutex);
  return NULL;
}

static int init_gate(qaplet_runtime *runtime, stop_gate *gate) {
  gate->runtime=runtime;
  CHECK(!pthread_mutex_init(&gate->mutex,NULL) && !pthread_cond_init(&gate->condition,NULL));
  qaplet_host_export items[]={QAPLET_HOST_EXPORT_INIT,QAPLET_HOST_EXPORT_INIT};
  items[0].name="arm"; items[0].name_length=3; items[0].kind=QAPLET_HOST_FUNCTION; items[0].function=arm; items[0].userdata=gate;
  items[1].name="mark"; items[1].name_length=4; items[1].kind=QAPLET_HOST_FUNCTION; items[1].function=mark; items[1].userdata=&gate->marks;
  qaplet_host_module module=QAPLET_HOST_MODULE_INIT;
  module.name="host:stop"; module.name_length=9; module.exports=items; module.export_count=2;
  CHECK(qaplet_register_host_module(runtime,&module,NULL)==QAPLET_OK);
  return 0;
}

static int dispose_gate(stop_gate *gate) {
  CHECK(!pthread_cond_destroy(&gate->condition) && !pthread_mutex_destroy(&gate->mutex));
  return 0;
}

static int running(void) {
  for (unsigned variant=0; variant<4; ++variant) {
    qaplet_config config=QAPLET_CONFIG_INIT;
    config.limits.execution_timeout_ms=5000;
    qaplet_runtime *runtime=NULL;
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
    stop_gate gate={.mode=variant<2 ? 1 : variant==2 ? 0 : 3};
    CHECK(!init_gate(runtime,&gate));
    CHECK(!settle(runtime,"import('host:stop').then(m=>{globalThis.h=m})"));
    const char *source=variant==0 ? "h.arm();for(;;){}" :
        variant==1 ? "(async()=>{await 0;h.arm();for(;;){}})()" :
        variant==2 ? "Promise.resolve().then(()=>{h.arm();h.mark()}).finally(()=>h.mark())" :
        "new Promise(()=>{}).finally(()=>h.mark())";
    pthread_t worker;
    if (variant==3) {
      CHECK(qaplet_eval(runtime,source,strlen(source),&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
      CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING && !progress.progressed);
      gate.entered=1;
      CHECK(!pthread_create(&worker,NULL,stop_worker,&gate));
      CHECK(!pthread_join(worker,NULL));
      CHECK(gate.accepted.lifecycle==QAPLET_STOP_REQUESTED && gate.accepted.phase==QAPLET_PHASE_PENDING);
      CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_EXECUTION_STOPPED && !progress.progressed);
    } else {
      CHECK(!pthread_create(&worker,NULL,stop_worker,&gate));
      qaplet_code code=qaplet_eval(runtime,source,strlen(source),&progress,NULL);
      if (variant) {
        CHECK(code==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
        code=qaplet_pump(runtime,&progress,NULL);
      }
      CHECK(code==QAPLET_EXECUTION_STOPPED && progress.phase==QAPLET_PHASE_DONE && progress.code==QAPLET_EXECUTION_STOPPED);
      CHECK(!pthread_join(worker,NULL));
      CHECK(gate.arms==1);
    }
    CHECK(!gate.failed && !gate.marks && gate.accepted.termination_reason==QAPLET_EXECUTION_STOPPED);
    if (variant<2) CHECK(gate.owner_cpu_ns>=20000000);
    CHECK(!stopped(runtime,0));
    CHECK(!gate.marks && !dispose_gate(&gate));
  }
  puts("stop running: measured JS CPU loops; async interruption; C callback barrier; idle PENDING: pass");
  return 0;
}

static int ordering(void) {
  qaplet_config config=QAPLET_CONFIG_INIT;
  config.limits.event_queue_slots=2; config.limits.event_payload_bytes=8;
  qaplet_runtime *runtime=NULL;
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  stop_gate gate={.fill_queue=1};
  CHECK(!init_gate(runtime,&gate));
  CHECK(!settle(runtime,"Promise.all([import('qaplet:events'),import('host:stop')]).then(([m,h])=>{m.onEvent(e=>{if(e.type===1){h.arm();Promise.resolve().then(()=>h.mark())}else h.mark()});globalThis.wait=new Promise(()=>{}).finally(()=>h.mark())})"));
  CHECK(qaplet_post_event(runtime,1,0,NULL,0,NULL)==QAPLET_OK);
  pthread_t worker;
  CHECK(!pthread_create(&worker,NULL,stop_worker,&gate));
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_EXECUTION_STOPPED && progress.phase==QAPLET_PHASE_DONE);
  CHECK(!pthread_join(worker,NULL) && !gate.failed && gate.arms==1 && !gate.marks);
  CHECK(gate.accepted.lifecycle==QAPLET_STOP_REQUESTED && gate.accepted.phase==QAPLET_PHASE_RUNNING &&
        gate.accepted.queue_depth==2 && gate.accepted.events_accepted==3 && gate.accepted.events_consumed==1 &&
        gate.accepted.events_rejected==1);
  CHECK(!stopped(runtime,2) && !gate.marks && !dispose_gate(&gate));
  puts("stop ordering: claimed event in C; saturated acceptance barrier; unclaimed work dropped; no retry: pass");
  return 0;
}

static int failed_close(qaplet_runtime *runtime, qaplet_code reason) {
  qaplet_snapshot before=QAPLET_SNAPSHOT_INIT, after=QAPLET_SNAPSHOT_INIT;
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_get_snapshot(runtime,&before,NULL)==QAPLET_OK && before.lifecycle==QAPLET_FAILED && before.termination_reason==reason);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK && qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&after,NULL)==QAPLET_OK && !memcmp(&before,&after,sizeof(before)));
  CHECK(qaplet_get_result(runtime,&result,NULL)==reason);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime,&after,NULL)==QAPLET_OK && after.lifecycle==QAPLET_CLOSED &&
        after.termination_reason==reason && !after.engine_current_bytes);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK);
  return 0;
}

static qaplet_code stop_then_allocate(qaplet_runtime *runtime, uint64_t call, size_t argc,
                                      const qaplet_value *argv, void *opaque) {
  (void)argc; (void)argv;
  unsigned *refused=opaque;
  /* A valid bridged value, deliberately larger than this instance's heap. */
  static const unsigned char bytes[QAPLET_VALUE_BYTES_MAX]={0};
  qaplet_value value=QAPLET_VALUE_INIT;
  value.type=QAPLET_VALUE_BYTES; value.data=bytes; value.length=sizeof(bytes);
  if (qaplet_request_stop(runtime,NULL)!=QAPLET_OK) return QAPLET_HOST_ERROR;
  if (qaplet_host_set_result(runtime,call,&value,NULL)==QAPLET_MEMORY_LIMIT) ++*refused;
  return QAPLET_OK;
}

static int causes(void) {
  /* Both possible record orders are forced while the worker is active.
   * Merely passing a nominal deadline never reorders a previously recorded stop. */
  for (unsigned first_stop=0; first_stop<2; ++first_stop) {
    qaplet_config config=QAPLET_CONFIG_INIT;
    config.limits.execution_timeout_ms=50;
    qaplet_runtime *runtime=NULL;
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
    stop_gate gate={.mode=first_stop ? 0 : 2,.callback_hold_ns=first_stop ? 75000000 : 0};
    CHECK(!init_gate(runtime,&gate));
    CHECK(!settle(runtime,"import('host:stop').then(m=>{globalThis.h=m})"));
    pthread_t worker;
    CHECK(!pthread_create(&worker,NULL,stop_worker,&gate));
    const char *source=first_stop ? "h.arm();42" : "h.arm();for(;;){}";
    qaplet_code reason=first_stop ? QAPLET_EXECUTION_STOPPED : QAPLET_DEADLINE_EXCEEDED;
    CHECK(qaplet_eval(runtime,source,strlen(source),&progress,NULL)==reason && progress.phase==QAPLET_PHASE_DONE);
    CHECK(!pthread_join(worker,NULL) && !gate.failed && gate.arms==1 && gate.accepted.termination_reason==reason);
    if (first_stop) CHECK(!stopped(runtime,0));
    else CHECK(!failed_close(runtime,reason));
    CHECK(!dispose_gate(&gate));
  }
  for (unsigned allocation=0; allocation<2; ++allocation) {
    qaplet_config config=QAPLET_CONFIG_INIT;
    if (allocation) config.limits.engine_heap_bytes=512*1024;
    else config.limits.execution_dispatches=4;
    qaplet_runtime *runtime=NULL;
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
    const char *source=allocation ? "try{new Uint8Array(2097152)}catch(e){};42" :
        "function spin(){Promise.resolve().then(spin)}spin();42";
    qaplet_code reason=allocation ? QAPLET_MEMORY_LIMIT : QAPLET_DISPATCH_LIMIT;
    qaplet_code code=qaplet_eval(runtime,source,strlen(source),&progress,NULL);
    if (!allocation) {
      CHECK(code==QAPLET_OK && progress.phase==QAPLET_PHASE_PENDING);
      code=qaplet_pump(runtime,&progress,NULL);
    }
    CHECK(code==reason && progress.phase==QAPLET_PHASE_DONE);
    CHECK(!failed_close(runtime,reason));
  }
  qaplet_config config=QAPLET_CONFIG_INIT;
  config.limits.engine_heap_bytes=512*1024;
  qaplet_runtime *runtime=NULL;
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  unsigned refused=0;
  qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;
  item.name="stopAllocate"; item.name_length=12; item.kind=QAPLET_HOST_FUNCTION;
  item.function=stop_then_allocate; item.userdata=&refused;
  qaplet_host_module module=QAPLET_HOST_MODULE_INIT;
  module.name="host:stop"; module.name_length=9; module.exports=&item; module.export_count=1;
  CHECK(qaplet_register_host_module(runtime,&module,NULL)==QAPLET_OK);
  CHECK(!settle(runtime,"import('host:stop').then(m=>{globalThis.h=m})"));
  const char source[]="h.stopAllocate()";
  CHECK(qaplet_eval(runtime,source,sizeof(source)-1,&progress,NULL)==QAPLET_EXECUTION_STOPPED && refused==1);
  CHECK(!stopped(runtime,0));
  puts("stop causes: forced stop/deadline record orders; dispatch/allocation preserved; repeat FAILED/CLOSED: pass");
  return 0;
}

int sdk_stop(const char *mode) {
  if (!strcmp(mode,"--stop")) return idle_and_queue();
  if (!strcmp(mode,"--stop-running")) return running();
  if (!strcmp(mode,"--stop-ordering")) return ordering();
  if (!strcmp(mode,"--stop-causes")) return causes();
  return 2;
}
