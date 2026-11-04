/* SPDX-License-Identifier: MIT */
/* Supplied public SDK boundary, with no private engine headers. */
#include "qaplet.h"
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "timer contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static uint64_t monotonic_ns(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
  return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
typedef struct { pthread_t owner; unsigned calls; int valid, self_cancel, reserved; uint64_t delivered_ns; } observed;
static void delivered(qaplet_runtime *runtime, const qaplet_timer *timer, void *opaque) {
  (void)runtime; (void)timer;
  observed *state = opaque;
  ++state->calls; state->delivered_ns = monotonic_ns();
  state->valid = state->delivered_ns && pthread_equal(state->owner, pthread_self());
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  state->valid &= qaplet_eval(runtime, "1", 1, &progress, NULL) == QAPLET_BUSY &&
    qaplet_pump(runtime, &progress, NULL) == QAPLET_BUSY && qaplet_close(runtime, NULL) == QAPLET_BUSY &&
    qaplet_destroy(runtime, NULL) == QAPLET_BUSY;
  if (state->self_cancel) {
    qaplet_timer refused = QAPLET_TIMER_INIT;
    qaplet_timer_snapshot snapshot = QAPLET_TIMER_SNAPSHOT_INIT;
    state->reserved = qaplet_timer_cancel(runtime, timer, NULL) == QAPLET_OK &&
      qaplet_timer_create(runtime, 0, delivered, state, &refused, NULL) == QAPLET_RESOURCE_LIMIT &&
      qaplet_get_timer_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.current == 1;
  }
}
typedef struct { qaplet_runtime *runtime; qaplet_timer timer; int valid; } thread_check;
static void *wrong_thread(void *opaque) {
  thread_check *state = opaque;
  qaplet_timer output = QAPLET_TIMER_INIT;
  qaplet_timer_snapshot snapshot = QAPLET_TIMER_SNAPSHOT_INIT;
  state->valid = qaplet_timer_create(state->runtime, 0, delivered, NULL, &output, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_interval_create(state->runtime, 1, delivered, NULL, &output, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_immediate_create(state->runtime, delivered, NULL, &output, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_timer_cancel(state->runtime, &state->timer, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_timer_ref(state->runtime, &state->timer, 0, NULL) == QAPLET_WRONG_THREAD &&
    qaplet_get_timer_snapshot(state->runtime, &snapshot, NULL) == QAPLET_OK;
  return NULL;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_quiesce(runtime, 0, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_m2_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  CHECK(!snapshot.engine_current_bytes && !snapshot.native_handles && !snapshot.native_uncertain_handles);
  qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
  CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && !timers.reserved_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}
typedef struct { char text[256]; unsigned length; uint64_t handoffs[256]; } trace_state;
static qaplet_code mark(qaplet_runtime *r, uint64_t call, size_t argc, const qaplet_value *argv, void *opaque) {
  (void)call; trace_state *s=opaque;
  if (argc!=1 || argv[0].type!=QAPLET_VALUE_STRING || argv[0].length!=1 || s->length==sizeof(s->text)) return QAPLET_HOST_ERROR;
  qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;
  if (qaplet_get_work_snapshot(r,&work,NULL)) return QAPLET_HOST_ERROR;
  s->handoffs[s->length]=work.pending_handoffs;
  s->text[s->length++]=*(const char *)argv[0].data;
  return QAPLET_OK;
}
static int register_marks(qaplet_runtime *r, trace_state *s) {
  qaplet_host_export e=QAPLET_HOST_EXPORT_INIT;e.name="mark";e.name_length=4;e.kind=QAPLET_HOST_FUNCTION;e.function=mark;e.userdata=s;
  qaplet_host_module m=QAPLET_HOST_MODULE_INIT;m.name="host:trace";m.name_length=10;m.exports=&e;m.export_count=1;
  CHECK(qaplet_register_host_module(r,&m,NULL)==QAPLET_OK);return 0;
}
static qaplet_code quick_work(qaplet_work_context *c,const void *p,size_t n,qaplet_work_output *o,void *u) {
  (void)c;(void)p;(void)n;(void)o;(void)u;return QAPLET_OK;
}
static int scheduling(int split) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  if (split) base.limits.pump_jobs=1;
  qaplet_runtime *r=NULL;qaplet_progress p=QAPLET_PROGRESS_INIT;trace_state trace={0};
  CHECK(qaplet_create_m2(&base,&config,&r,NULL)==QAPLET_OK && !register_marks(r,&trace));
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;h.name="quick";h.name_length=5;h.function=quick_work;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);
  const char *source=split ?
    "Promise.all([import('qaplet:timers'),import('qaplet:events'),import('host:trace')]).then(([t,e,h])=>{e.onEvent(()=>h.mark('E'));globalThis.install=()=>{for(let i=0;i<32;++i){t.setTimeout(()=>{h.mark('T');Promise.resolve().then(()=>h.mark('1')).then(()=>h.mark('2')).then(()=>h.mark('3'))},0);t.setImmediate(()=>h.mark('I'))}}})" :
    "Promise.all([import('qaplet:timers'),import('qaplet:events'),import('host:trace')]).then(([t,e,h])=>{e.onEvent(()=>h.mark('E'));globalThis.install=()=>{for(let i=0;i<32;++i){t.setTimeout(()=>h.mark('T'),0);t.setImmediate(()=>h.mark('I'))}}})";
  CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK);
  while (p.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(r,"install();42",12,&p,NULL)==QAPLET_OK);
  qaplet_work_task tasks[32]={{0}};
  for (unsigned i=0;i<32;++i) {
    CHECK(qaplet_work_submit(r,"quick",5,NULL,0,&tasks[i],NULL)==QAPLET_OK);
    CHECK(qaplet_post_event(r,i,0,NULL,0,NULL)==QAPLET_OK);
  }
  qaplet_work_snapshot w=QAPLET_WORK_SNAPSHOT_INIT;
  do { CHECK(qaplet_get_work_snapshot(r,&w,NULL)==QAPLET_OK);sched_yield(); } while (w.completed<32);
  CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  qaplet_snapshot events=QAPLET_SNAPSHOT_INIT;qaplet_timer_snapshot timers=QAPLET_TIMER_SNAPSHOT_INIT;
  if (!split) {
    CHECK(trace.length==24 && !memcmp(trace.text,"TIETIETIETIETIETIETIETIE",24));
    CHECK(trace.handoffs[0]==31 && trace.handoffs[2]==31 && trace.handoffs[3]==30 && trace.handoffs[23]==24);
    CHECK(qaplet_get_work_snapshot(r,&w,NULL)==QAPLET_OK && w.pending_handoffs==24);
    CHECK(qaplet_get_snapshot(r,&events,NULL)==QAPLET_OK && events.events_consumed==8);
    CHECK(qaplet_get_timer_snapshot(r,&timers,NULL)==QAPLET_OK && timers.current==48);
  } else {
    CHECK(p.phase==QAPLET_PHASE_PENDING && trace.length==2 && !memcmp(trace.text,"T1",2) && trace.handoffs[0]==31);
    CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK && p.phase==QAPLET_PHASE_PENDING);
    CHECK(trace.length==3 && !memcmp(trace.text,"T12",3));
    CHECK(qaplet_get_snapshot(r,&events,NULL)==QAPLET_OK && !events.events_consumed);
    CHECK(qaplet_get_work_snapshot(r,&w,NULL)==QAPLET_OK && w.pending_handoffs==31);
    CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK && p.phase==QAPLET_PHASE_PENDING);
    CHECK(trace.length==7 && !memcmp(trace.text,"T123IET",7));
    CHECK(trace.handoffs[4]==31 && trace.handoffs[5]==31 && trace.handoffs[6]==30);
    CHECK(qaplet_get_snapshot(r,&events,NULL)==QAPLET_OK && events.events_consumed==1);
  }
  CHECK(qaplet_close(r,NULL)==QAPLET_OK);
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_quiesce(r,2000,&d,NULL)==QAPLET_OK && d.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);
  puts(split ? "M2 scheduling checkpoints: jobs-only pumps preserve four-category cursor; public callback order and handoffs: pass" :
    "M2 scheduling fairness: completion/timer/immediate/event share 32 deliveries; public bounded order within one pump: pass");
  return 0;
}
static int checkpoint_errors(void) {
  qaplet_config b=QAPLET_CONFIG_INIT;b.limits.pump_jobs=1;
  qaplet_m2_config c=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;trace_state trace={0};
  qaplet_progress p=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_create_m2(&b,&c,&r,NULL)==QAPLET_OK && !register_marks(r,&trace));
  const char *s="Promise.all([import('qaplet:timers'),import('host:trace')]).then(([t,h])=>{globalThis.install=bad=>{t.setImmediate(()=>{h.mark('T');let rejected=Promise.reject(Error('handled later'));Promise.resolve().then(()=>h.mark('1')).then(()=>h.mark('2')).then(()=>h.mark('3')).then(()=>rejected.catch(()=>h.mark('H')));if(bad)throw Error('ordinary')});t.setImmediate(()=>h.mark('B'))}})";
  CHECK(qaplet_eval(r,s,strlen(s),&p,NULL)==QAPLET_OK);
  while (p.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  for (unsigned bad=0;bad<2;++bad) {
    trace.length=0;
    s=bad ? "install(true);42" : "install(false);42";
    CHECK(qaplet_eval(r,s,strlen(s),&p,NULL)==QAPLET_OK);
    qaplet_code code=QAPLET_OK;
    for (unsigned i=0;i<20;++i) {
      code=qaplet_pump(r,&p,NULL);
      if (code || p.phase==QAPLET_PHASE_DONE) break;
      CHECK(trace.length<6);
    }
    CHECK(code==(bad ? QAPLET_JS_EXCEPTION : QAPLET_OK));
    CHECK(trace.length==(bad ? 5 : 6) && !memcmp(trace.text,bad ? "T123H" : "T123HB",trace.length));
    if (bad) {
      qaplet_snapshot stats=QAPLET_SNAPSHOT_INIT;
      CHECK(qaplet_get_snapshot(r,&stats,NULL)==QAPLET_OK && stats.lifecycle==QAPLET_OPEN);
      CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK && trace.length==6 && trace.text[5]=='B');
      CHECK(qaplet_get_snapshot(r,&stats,NULL)==QAPLET_OK && stats.execution_dispatches==1);
    }
  }
  CHECK(!finish(r));
  puts("M2 scheduling errors: late handled rejection waits full cross-pump checkpoint; ordinary throw settles jobs then returns; next independent callback ledger recovers: pass");return 0;
}
static int infinite_ledgers(void) {
  for (unsigned active=0;active<2;++active) {
    qaplet_config b=QAPLET_CONFIG_INIT;b.limits.pump_jobs=1;b.limits.execution_dispatches=50;
    qaplet_m2_config c=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;qaplet_progress p=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_create_m2(&b,&c,&r,NULL)==QAPLET_OK);
    const char *s="import('qaplet:timers').then(t=>{globalThis.install=()=>t.setImmediate(()=>{function again(){Promise.resolve().then(again)}again()})})";
    CHECK(qaplet_eval(r,s,strlen(s),&p,NULL)==QAPLET_OK);
    while (p.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
    s=active ? "install();new Promise(()=>{})" : "install();42";
    CHECK(qaplet_eval(r,s,strlen(s),&p,NULL)==QAPLET_OK);
    qaplet_code code=QAPLET_OK;
    for (unsigned i=0;i<60 && !code;++i) code=qaplet_pump(r,&p,NULL);
    qaplet_snapshot stats=QAPLET_SNAPSHOT_INIT;
    CHECK(code==QAPLET_DISPATCH_LIMIT && qaplet_get_snapshot(r,&stats,NULL)==QAPLET_OK && stats.execution_dispatches==50 && stats.lifecycle==QAPLET_FAILED);
    CHECK(!finish(r));
  }
  puts("M2 scheduling ledgers: active entry and independent idle callback accumulate jobs across pumps; infinite jobs terminate at exact dispatch limit: pass");return 0;
}
static int discarded_handles(void) {
  qaplet_config b=QAPLET_CONFIG_INIT;b.limits.engine_heap_bytes=4*1024*1024;
  qaplet_m2_config c=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;qaplet_progress p=QAPLET_PROGRESS_INIT;trace_state trace={0};
  CHECK(qaplet_create_m2(&b,&c,&r,NULL)==QAPLET_OK && !register_marks(r,&trace));
  const char *s="Promise.all([import('qaplet:timers'),import('host:trace')]).then(([t,h])=>{globalThis.install=()=>{t.setTimeout(()=>h.mark('T'),0);t.setImmediate(()=>h.mark('I'));for(let i=0;i<1000;++i){const a={bytes:new ArrayBuffer(65536)};a.self=a}}})";
  CHECK(qaplet_eval(r,s,strlen(s),&p,NULL)==QAPLET_OK);
  while (p.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  /* 64 MiB of unreachable cyclic ArrayBuffer payloads under a 4 MiB engine
   * budget requires cycle collection; plain reference counting cannot reclaim
   * these cycles. The two discarded resource handles must remain scheduled. */
  CHECK(qaplet_eval(r,"install();42",12,&p,NULL)==QAPLET_OK && !trace.length);
  CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK && trace.length==2 && !memcmp(trace.text,"TI",2));
  CHECK(!finish(r));
  puts("M2 scheduling GC: discarded timer/immediate handles survive cyclic garbage collection under finite engine budget: pass");return 0;
}
int sdk_timers(const char *mode, const char *modules) {
  (void)modules;
  if (!strcmp(mode,"--m2-timers-fairness")) return scheduling(0);
  if (!strcmp(mode,"--m2-timers-checkpoints")) return scheduling(1);
  if (!strcmp(mode,"--m2-timers-errors")) return checkpoint_errors();
  if (!strcmp(mode,"--m2-timers-ledgers")) return infinite_ledgers();
  if (!strcmp(mode,"--m2-timers-gc")) return discarded_handles();
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_timer timer = QAPLET_TIMER_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  observed state = {.owner = pthread_self()};
  if (!strcmp(mode, "--m2-timers-limits")) config.limits.timers = 1;
  if (!strcmp(mode, "--m2-timers-shared-limits")) config.limits.timers = 1;
  if (!strcmp(mode, "--m2-timers-jobs")) base.limits.pump_jobs = 1;
  if (!strcmp(mode, "--m2-timers-deadline")) base.limits.execution_timeout_ms = 10;
  if (!strcmp(mode, "--m2-timers-budget")) { base.limits.execution_dispatches = 20; base.limits.pump_jobs = 1; }
  CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
  if (!strcmp(mode,"--m2-timers-shared-limits")) {
    qaplet_timer refused={7,8,9};qaplet_timer_snapshot timers=QAPLET_TIMER_SNAPSHOT_INIT;
    struct {uint32_t struct_size,abi_version;uint64_t canary;} short_d={8,1,42};
    CHECK(qaplet_interval_create(runtime,0,delivered,&state,&refused,(qaplet_diagnostic *)&short_d)==QAPLET_ABI_MISMATCH && short_d.canary==42);
    CHECK(qaplet_interval_create(runtime,0,delivered,&state,&refused,NULL)==QAPLET_INVALID_ARGUMENT && refused.runtime_id==7);
    CHECK(qaplet_interval_create(runtime,1,delivered,&state,&timer,NULL)==QAPLET_OK);
    CHECK(qaplet_immediate_create(runtime,delivered,&state,&refused,NULL)==QAPLET_RESOURCE_LIMIT && refused.runtime_id==7);
    CHECK(qaplet_timer_cancel(runtime,&timer,NULL)==QAPLET_OK);
    CHECK(qaplet_immediate_create(runtime,delivered,&state,&timer,NULL)==QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime,&timer,0,NULL)==QAPLET_OK);
    CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && !state.calls);
    state.self_cancel=1;qaplet_progress p=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_pump(runtime,&p,NULL)==QAPLET_OK && state.calls==1 && state.reserved);
    CHECK(qaplet_interval_create(runtime,1,delivered,&state,&timer,NULL)==QAPLET_OK);
    state.reserved=0;
    CHECK(qaplet_timer_ref(runtime,&timer,0,NULL)==QAPLET_OK);
    CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && state.calls==1);
    CHECK(qaplet_timer_ref(runtime,&timer,1,NULL)==QAPLET_OK);
    CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && state.calls==2 && state.reserved);
    CHECK(qaplet_get_timer_snapshot(runtime,&timers,NULL)==QAPLET_OK && !timers.current && timers.peak==1);
    CHECK(!finish(runtime));
    CHECK(qaplet_create(&base,&runtime,NULL)==QAPLET_OK);
    CHECK(qaplet_interval_create(runtime,1,delivered,&state,&timer,NULL)==QAPLET_UNSUPPORTED);
    CHECK(qaplet_immediate_create(runtime,delivered,&state,&timer,NULL)==QAPLET_UNSUPPORTED);
    CHECK(qaplet_close(runtime,NULL)==QAPLET_OK && qaplet_destroy(runtime,NULL)==QAPLET_OK);
    puts("M2 scheduling resource budget: timer/interval/immediate share finite slots; callback cancel retains slot; unref-only natural return and explicit pump; terminal cleanup: pass");return 0;
  }
  if (!strcmp(mode, "--m2-timers-immediate")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setImmediate,clearImmediate})=>{globalThis.trace='';let sync=true;globalThis.install=()=>{setImmediate(()=>{if(sync)throw Error('sync');trace+='a';setImmediate(()=>trace+='c')});let t=setImmediate(()=>trace+='x');clearImmediate(t);setImmediate(()=>trace+='b');sync=false};return 42})";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    while (progress.phase == QAPLET_PHASE_PENDING) CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_eval(runtime, "install();42", 12, &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
    CHECK(qaplet_eval(runtime, "trace", 5, &progress, NULL) == QAPLET_OK);
    char text[8]; result.buffer=text;result.capacity=sizeof(text);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.length==2 && !memcmp(text,"ab",2));
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed);
    CHECK(qaplet_eval(runtime, "trace", 5, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.length==3 && !memcmp(text,"abc",3));
    CHECK(!finish(runtime));
    puts("M2 immediate: asynchronous registration; FIFO; nested callback waits next pump; discarded handles retained; cancel; cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-interval")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setInterval,setTimeout})=>new Promise(resolve=>{let n=0;for(const d of [0,-1,0.5,'1',NaN,Infinity]){try{setInterval(()=>{},d);throw Error('accepted')}catch(e){if(e.code!==1)throw e}}let t=setInterval(()=>{if(++n===3){t.cancel();setTimeout(()=>resolve(n),3)}return new Promise(()=>{})},1)}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 3);
    CHECK(!finish(runtime));
    puts("M2 interval: strict minimum 1ms; repeated original schedule; ignored Promise; self cancel; cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-liveness")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    CHECK(qaplet_eval(runtime, "42", 2, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime, &timer, 0, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && !state.calls);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(qaplet_pump(runtime, &progress, NULL) == QAPLET_OK && progress.progressed && state.calls == 1);
    CHECK(qaplet_timer_create(runtime, 5000, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime, &timer, 0, NULL) == QAPLET_OK);
    CHECK(qaplet_run_once(runtime, 10000, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_IDLE && state.calls == 1);
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_ref(runtime, &timer, 0, NULL) == QAPLET_OK);
    CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == QAPLET_OK && drive.progressed && state.calls == 2);
    uint64_t registered_ns = monotonic_ns();
    CHECK(registered_ns);
    CHECK(qaplet_timer_create(runtime, 15, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_run_once(runtime, 1, &drive, NULL) == QAPLET_OK);
    if (state.calls == 2) {
      qaplet_timer_snapshot waiting = QAPLET_TIMER_SNAPSHOT_INIT;
      CHECK(drive.reason == QAPLET_DRIVE_WAIT_TIMEOUT);
      CHECK(qaplet_get_timer_snapshot(runtime, &waiting, NULL) == QAPLET_OK && waiting.current == 1 && waiting.referenced == 1);
      CHECK(qaplet_run_once(runtime, 1000, &drive, NULL) == QAPLET_OK && drive.reason == QAPLET_DRIVE_PROGRESS && state.calls == 3);
    } else {
      /* The wait budget is not a hard wall-clock limit: scheduler suspension
       * or an OS wait overrun may cross the accepted timer deadline. */
      CHECK(drive.reason == QAPLET_DRIVE_PROGRESS && state.calls == 3);
    }
    CHECK(state.valid && state.delivered_ns >= registered_ns + UINT64_C(15000000));
    CHECK(qaplet_timer_create(runtime, 5000, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_request_stop(runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_EXECUTION_STOPPED && state.calls == 3);
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && !timers.reserved_bytes);
    CHECK(!finish(runtime));
    puts("M2 timer liveness: DONE ref wait; unref run/pump/run_once; closest wait; stop retirement: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-limits")) {
    qaplet_timer refused = {7, 8, 9};
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    CHECK(qaplet_timer_create(runtime, UINT64_MAX, delivered, &state, &refused, NULL) == QAPLET_INVALID_ARGUMENT && refused.runtime_id == 7);
    CHECK(qaplet_timer_create(runtime, UINT64_C(9007199254740991), delivered, &state, &refused, NULL) == QAPLET_INVALID_ARGUMENT);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &refused, NULL) == QAPLET_RESOURCE_LIMIT && refused.runtime_id == 7);
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    state.self_cancel = 1;
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && state.calls == 1 && state.reserved);
    state.self_cancel = 0;
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && timers.peak == 1);
    CHECK(!finish(runtime));
    config.limits.timers = 2147483647;
    qaplet_m2_diagnostic detail = QAPLET_M2_DIAGNOSTIC_INIT;
    CHECK(qaplet_create_m2_with_diagnostic(&base, &config, &runtime, &detail, NULL) == QAPLET_RESOURCE_LIMIT && !runtime && detail.creation_rollback == QAPLET_ROLLBACK_COMPLETE);
    puts("M2 timer limits: count reuse; strict C delay; metadata total-storage precheck; rollback: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-strict")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout,clearTimeout})=>{let hooks=0,n=0;for(const d of [NaN,Infinity,-1,0.5,'1',null,undefined,1n,Symbol(),9007199254740992,9007199254740991,{valueOf(){++hooks;return 0}}]){try{setTimeout(()=>{},d)}catch(e){if(e.code!==1)throw e;++n}}let t=setTimeout(()=>{},5000);if(!t.hasRef())throw Error();t.unref();if(t.hasRef())throw Error();t.ref();clearTimeout(t);try{t.cancel()}catch(e){if(e.code!==25)throw e;++n}return hooks===0&&n===13})";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_BOOL && result.boolean == 1);
    CHECK(!finish(runtime));
    puts("M2 timer strict JS: finite safe integer only; no coercion hooks; deadline overflow; handle controls: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-jobs")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(resolve=>{let s='';setTimeout(()=>{s+='a';Promise.resolve().then(()=>s+='1').then(()=>s+='2')},0);setTimeout(()=>{if(s!=='a12')throw Error('checkpoint');resolve(42)},0)}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    source = "import('qaplet:timers').then(({setTimeout})=>{globalThis.afterTimer=0;setTimeout(()=>{Promise.resolve().then(()=>afterTimer=1);throw Error('timer')},0);setTimeout(()=>{if(afterTimer!==1)throw Error('jobs lost');afterTimer=2},0)})";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_JS_EXCEPTION && drive.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK);
    CHECK(qaplet_eval(runtime, "afterTimer", 10, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 2);
    CHECK(!finish(runtime));
    puts("M2 timer jobs: complete cross-pump checkpoints; ordinary error settles jobs; subsequent drive: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-deadline")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(resolve=>setTimeout(resolve,5000)))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_DEADLINE_EXCEEDED && drive.reason == QAPLET_DRIVE_ERROR);
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && !timers.reserved_bytes);
    CHECK(!finish(runtime));
    puts("M2 timer deadline: active entry absolute deadline bounds later timer; terminal retirement: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-budget")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(()=>{function again(){setTimeout(again,0)}again()}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    qaplet_code code = QAPLET_OK;
    for (unsigned i = 0; i < 30 && !code; ++i) code = qaplet_run_once(runtime, 0, &drive, NULL);
    CHECK(code == QAPLET_DISPATCH_LIMIT);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.execution_dispatches == 20 && snapshot.lifecycle == QAPLET_FAILED);
    CHECK(!finish(runtime));
    puts("M2 timer budget: active entry dispatch ledger accumulates across zero-wait drives; terminal cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-tla")) {
    char path[4096];
    CHECK(modules && snprintf(path, sizeof(path), "%s/timer-tla.mjs", modules) > 0);
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    CHECK(qaplet_run_entry(runtime, path, strlen(path), &progress, NULL) == QAPLET_OK && progress.phase == QAPLET_PHASE_PENDING);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.phase == QAPLET_PHASE_DONE);
    CHECK(qaplet_eval(runtime, "timerTla", 8, &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.number == 42);
    CHECK(!finish(runtime));
    puts("M2 timer TLA: real ESM entry waits and resumes; cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-identity")) {
    qaplet_timer_snapshot timers = QAPLET_TIMER_SNAPSHOT_INIT;
    qaplet_runtime *other = NULL;
    struct { uint32_t struct_size, abi_version; uint64_t canary; } short_diagnostic = {8, 1, UINT64_C(0x123456789abcdef0)};
    CHECK(qaplet_timer_cancel(runtime, &timer, (qaplet_diagnostic *)&short_diagnostic) == QAPLET_ABI_MISMATCH && short_diagnostic.canary == UINT64_C(0x123456789abcdef0));
    CHECK(qaplet_create_m2(&base, &config, &other, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 1000, delivered, &state, &timer, NULL) == QAPLET_OK);
    thread_check check = {runtime, timer, 0}; pthread_t worker;
    CHECK(!pthread_create(&worker, NULL, wrong_thread, &check) && !pthread_join(worker, NULL) && check.valid);
    CHECK(qaplet_get_timer_snapshot(runtime, (qaplet_timer_snapshot *)&short_diagnostic, NULL) == QAPLET_ABI_MISMATCH && short_diagnostic.canary == UINT64_C(0x123456789abcdef0));
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && timers.current == 1 && timers.referenced == 1 && !timers.ready && timers.next_timeout_ms > 0 && timers.next_timeout_ms <= 1000);
    CHECK(qaplet_timer_cancel(other, &timer, NULL) == QAPLET_INVALID_HANDLE);
    qaplet_timer stale = timer;
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK);
    CHECK(qaplet_timer_cancel(runtime, &stale, NULL) == QAPLET_INVALID_HANDLE);
    CHECK(qaplet_timer_ref(runtime, &stale, 0, NULL) == QAPLET_INVALID_HANDLE);
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && timers.current == 1 && timers.ready == 1);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && state.calls == 1 && state.valid);
    CHECK(qaplet_timer_cancel(runtime, &timer, NULL) == QAPLET_INVALID_HANDLE);
    CHECK(qaplet_get_timer_snapshot(runtime, &timers, NULL) == QAPLET_OK && !timers.current && timers.peak == 1 && timers.next_timeout_ms == UINT64_MAX);
    CHECK(!finish(runtime) && !finish(other));
    puts("M2 timer identity: Runtime/generation isolation; stale completion; ready/ref/deadline POD; cleanup: pass");
    return 0;
  }
  if (!strcmp(mode, "--m2-timers-js")) {
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    qaplet_result result = QAPLET_RESULT_INIT;
    const char *source = "import('qaplet:timers').then(({setTimeout})=>new Promise(resolve=>{globalThis.trace='registered';setTimeout(()=>{if(trace!=='registered')throw Error('sync');resolve(42)},5)}))";
    CHECK(qaplet_eval(runtime, source, strlen(source), &progress, NULL) == QAPLET_OK);
    CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && drive.phase == QAPLET_PHASE_DONE && drive.reason == QAPLET_DRIVE_IDLE);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_DOUBLE && result.number == 42);
    CHECK(!finish(runtime));
    puts("M2 timer JS: registered callback advances entry Promise; cleanup: pass");
    return 0;
  }
  CHECK(!strcmp(mode, "--m2-timers"));
  CHECK(qaplet_timer_create(runtime, 0, delivered, &state, &timer, NULL) == QAPLET_OK && !state.calls);
  CHECK(qaplet_run_once(runtime, 0, &drive, NULL) == QAPLET_OK && drive.progressed && state.calls == 1 && state.valid);
  CHECK(qaplet_run(runtime, &drive, NULL) == QAPLET_OK && !drive.progressed && state.calls == 1);
  CHECK(!finish(runtime));
  puts("M2 timers: zero-delay asynchronous owner callback; exactly once; cleanup: pass");
  return 0;
}
#ifdef SDK_STANDALONE
int main(int argc, char **argv) { return sdk_timers(argc > 1 ? argv[1] : "--m2-timers", argc > 2 ? argv[2] : NULL); }
#endif
