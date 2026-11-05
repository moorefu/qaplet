/* SPDX-License-Identifier: MIT */
/* Mixed lifetime contracts use only the delivered SDK and real Host resources. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/console.h"
#include "libc/nt/runtime.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern int __mkntpath(const char *,char16_t *);

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"mixed contract line %d: %s (errno %d)\n",__LINE__,#c,errno); return 1; } } while (0)
#ifndef MIXED_FAULT_STAGE
#define MIXED_FAULT_STAGE(stage,r) ((void)0)
#define MIXED_CLEANUP_CODE QAPLET_OK
#define MIXED_UNKNOWN 0
#define MIXED_NOTIFY notify
#endif
typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t condition;
  unsigned entered, released, probe, probed, cancelled, exited, calls, forbidden;
  unsigned magic;
} mixed_gate;
#define MIXED_GATE_INIT {PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0,0,0,0,0,0,0,0x1234abcd}
static qaplet_code mixed_work(qaplet_work_context *context,const void *input,size_t n,
                               qaplet_work_output *output,void *opaque) {
  mixed_gate *g=opaque;
  pthread_mutex_lock(&g->mutex);++g->entered;pthread_cond_broadcast(&g->condition);
  while (!g->released) {
    if (g->probe!=g->probed) { g->cancelled=qaplet_work_cancelled(context);g->probed=g->probe;pthread_cond_broadcast(&g->condition); }
    pthread_cond_wait(&g->condition,&g->mutex);
  }
  int valid=g->magic==0x1234abcd && n==1 && *(const unsigned char *)input==17 && output->capacity>=1;
  g->cancelled=qaplet_work_cancelled(context);++g->exited;
  pthread_cond_broadcast(&g->condition);pthread_mutex_unlock(&g->mutex);
  if (!valid) return QAPLET_HOST_ERROR;
  *(unsigned char *)output->data=42;output->length=1;return QAPLET_OK;
}
static void wait_entered(mixed_gate *g) { pthread_mutex_lock(&g->mutex);while(!g->entered)pthread_cond_wait(&g->condition,&g->mutex);pthread_mutex_unlock(&g->mutex); }
static unsigned probe_cancel(mixed_gate *g) { pthread_mutex_lock(&g->mutex);++g->probe;pthread_cond_broadcast(&g->condition);while(g->probed!=g->probe)pthread_cond_wait(&g->condition,&g->mutex);unsigned result=g->cancelled;pthread_mutex_unlock(&g->mutex);return result; }
static void release_work(mixed_gate *g) { pthread_mutex_lock(&g->mutex);g->released=1;pthread_cond_broadcast(&g->condition);pthread_mutex_unlock(&g->mutex); }
typedef struct { unsigned calls;FILE *resource; } mixed_binding;
static unsigned forbidden_calls;
static qaplet_code mark(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *argv,void *opaque) {
  (void)r;(void)call;(void)argc;(void)argv;mixed_binding *binding=opaque;++binding->calls;
  return fputc('B',binding->resource)!=EOF ? QAPLET_OK : QAPLET_HOST_ERROR;
}
static qaplet_code forbidden(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *argv,void *opaque) {
  (void)r;(void)call;(void)argc;(void)argv;(void)opaque;++forbidden_calls;return QAPLET_OK;
}
static unsigned echo_calls;
static qaplet_code echo_work(qaplet_work_context *context,const void *input,size_t n,qaplet_work_output *output,void *opaque) {
  (void)context;(void)opaque;
  if(n!=1 || !output->capacity)return QAPLET_HOST_ERROR;
  __atomic_add_fetch(&echo_calls,1,__ATOMIC_RELAXED);*(unsigned char *)output->data=*(const unsigned char *)input;output->length=1;return QAPLET_OK;
}
static int register_mixed(qaplet_runtime *r,mixed_gate *g,mixed_binding *binding) {
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;h.name="held";h.name_length=4;h.function=mixed_work;h.userdata=g;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);
  h.name="echo";h.function=echo_work;CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);
  qaplet_host_export e[2]={QAPLET_HOST_EXPORT_INIT,QAPLET_HOST_EXPORT_INIT};
  e[0].name="mark";e[0].name_length=4;e[0].kind=QAPLET_HOST_FUNCTION;e[0].function=mark;e[0].userdata=binding;
  e[1].name="forbidden";e[1].name_length=9;e[1].kind=QAPLET_HOST_FUNCTION;e[1].function=forbidden;e[1].userdata=binding;
  qaplet_host_module m=QAPLET_HOST_MODULE_INIT;m.name="host:mixed";m.name_length=10;m.exports=e;m.export_count=2;
  CHECK(qaplet_register_host_module(r,&m,NULL)==QAPLET_OK);return 0;
}
static int evaluate(qaplet_runtime *r,const char *source) {
  qaplet_progress p=QAPLET_PROGRESS_INIT;CHECK(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK);
  for(unsigned i=0;p.phase==QAPLET_PHASE_PENDING && i<100;++i)CHECK(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  CHECK(p.phase==QAPLET_PHASE_DONE);return 0;
}
static void notify(void *opaque) { __atomic_add_fetch((unsigned *)opaque,1,__ATOMIC_RELAXED); }
typedef struct { qaplet_runtime *r;int code; } stop_request;
static void *request_stop(void *opaque) { stop_request *s=opaque;s->code=qaplet_request_stop(s->r,NULL);return NULL; }
static double milliseconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1000000.0; }
static int wait_work(qaplet_runtime *r,qaplet_work_task *task,int handoff) {
  qaplet_work_status s=QAPLET_WORK_STATUS_INIT;qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;
  double until=milliseconds()+5000;
  do { CHECK(qaplet_get_work_status(r,task,&s,NULL)==QAPLET_OK);if(s.state>=QAPLET_WORK_SUCCEEDED && !s.physical_pending && (!handoff || !s.pending_handoff))return 0;if(handoff)CHECK(qaplet_run_once(r,0,&d,NULL)==QAPLET_OK); }while(milliseconds()<until);
  CHECK(0);return 1;
}
static int mixed_races(qaplet_runtime *r,mixed_gate *g) {
  unsigned char byte=17;qaplet_work_task done=QAPLET_WORK_TASK_INIT,next=QAPLET_WORK_TASK_INIT,held=QAPLET_WORK_TASK_INIT,queued=QAPLET_WORK_TASK_INIT;
  CHECK(qaplet_work_submit(r,"echo",4,&byte,1,&done,NULL)==QAPLET_OK && !wait_work(r,&done,0));
  CHECK(qaplet_work_cancel(r,&done,NULL)==QAPLET_ALREADY_COMPLETED && !wait_work(r,&done,1));
  unsigned char copied=0;qaplet_work_result result=QAPLET_WORK_RESULT_INIT;result.buffer=&copied;result.capacity=1;
  CHECK(qaplet_work_copy_result(r,&done,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_SUCCEEDED && copied==17 && echo_calls==1);
  CHECK(qaplet_work_release(r,&done,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(r,"echo",4,&byte,1,&next,NULL)==QAPLET_OK && next.slot==done.slot && next.generation!=done.generation);
  CHECK(qaplet_work_cancel(r,&done,NULL)==QAPLET_INVALID_HANDLE && !wait_work(r,&next,1));
  CHECK(qaplet_work_copy_result(r,&next,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_SUCCEEDED && copied==17 && echo_calls==2 && qaplet_work_release(r,&next,NULL)==QAPLET_OK);
  CHECK(qaplet_work_submit(r,"held",4,&byte,1,&held,NULL)==QAPLET_OK);wait_entered(g);
  CHECK(qaplet_work_submit(r,"echo",4,&byte,1,&queued,NULL)==QAPLET_OK && qaplet_work_cancel(r,&queued,NULL)==QAPLET_OK && qaplet_work_cancel(r,&queued,NULL)==QAPLET_OK);
  CHECK(!wait_work(r,&queued,1) && qaplet_work_copy_result(r,&queued,&result,NULL)==QAPLET_OK && result.state==QAPLET_WORK_CANCELLED && result.code==QAPLET_CANCELLED && echo_calls==2);
  CHECK(qaplet_work_release(r,&queued,NULL)==QAPLET_OK && qaplet_work_cancel(r,&queued,NULL)==QAPLET_INVALID_HANDLE);
  release_work(g);CHECK(!wait_work(r,&held,1) && qaplet_work_release(r,&held,NULL)==QAPLET_OK && g->entered==1 && g->exited==1);
  pthread_mutex_lock(&g->mutex);g->entered=g->exited=g->released=0;pthread_mutex_unlock(&g->mutex);
  return 0;
}

typedef struct {
  char path[64];int file,saved_out,saved_in,master,slave,terminal;
  int64_t file_handle,saved_out_handle,console,saved_in_handle;
  struct termios before;uint32_t mode;int flags;
} mixed_stdio;
static int same_mode(const struct termios *a,const struct termios *b) {
  return a->c_iflag==b->c_iflag && a->c_oflag==b->c_oflag && a->c_cflag==b->c_cflag && a->c_lflag==b->c_lflag &&
    !memcmp(a->c_cc,b->c_cc,sizeof(a->c_cc)) && cfgetispeed(a)==cfgetispeed(b) && cfgetospeed(a)==cfgetospeed(b);
}
static int record_terminal(mixed_stdio *f,const char *stage) {
  if(!f->terminal)return 0;
  FILE *out=fopen("mixed-terminal.ndjson","ab");CHECK(out);
  if(IsWindows()) { uint32_t mode;CHECK(GetConsoleMode(f->console,&mode));fprintf(out,"{\"stage\":\"%s\",\"mode\":%u}\n",stage,mode); }
  else {
    struct termios t;CHECK(!tcgetattr(f->slave,&t));
    fprintf(out,"{\"stage\":\"%s\",\"iflag\":%llu,\"oflag\":%llu,\"cflag\":%llu,\"lflag\":%llu,\"ispeed\":%u,\"ospeed\":%u,\"flags\":%d,\"cc\":[",stage,(unsigned long long)t.c_iflag,(unsigned long long)t.c_oflag,(unsigned long long)t.c_cflag,(unsigned long long)t.c_lflag,(unsigned)cfgetispeed(&t),(unsigned)cfgetospeed(&t),fcntl(f->slave,F_GETFL));
    for(unsigned i=0;i<sizeof(t.c_cc);++i)fprintf(out,"%s%u",i ? "," : "",(unsigned char)t.c_cc[i]);fputs("]}\n",out);
  }
  CHECK(!fclose(out));return 0;
}
static int mixed_success(const char *text) {
  FILE *out=fopen("mixed-result.txt","wb");CHECK(out && fputs(text,out)>=0 && fputc('\n',out)!=EOF && !fclose(out));puts(text);return 0;
}
static int prepare_stdio(mixed_stdio *f,int terminal) {
  memset(f,0,sizeof(*f));f->terminal=terminal;strcpy(f->path,"./qaplet-mixed-XXXXXX");f->file=mkstemp(f->path);CHECK(f->file>=0);
  if(IsWindows()) {
    char16_t native[4096];CHECK(__mkntpath(f->path,native)>=0);
    f->file_handle=CreateFile(native,0xc0000000u,7,NULL,3,0x80,0);CHECK(f->file_handle!=-1);
    f->saved_out_handle=GetStdHandle(-11);CHECK(SetStdHandle(-11,f->file_handle));
    if(terminal) { f->console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);CHECK(f->console!=-1 && GetConsoleMode(f->console,&f->mode));f->saved_in_handle=GetStdHandle(-10);CHECK(SetStdHandle(-10,f->console)); }
  } else {
    f->saved_out=dup(1);CHECK(f->saved_out>=0 && dup2(f->file,1)==1);
    if(terminal) {
      f->master=posix_openpt(O_RDWR|O_NOCTTY);CHECK(f->master>=0 && !grantpt(f->master) && !unlockpt(f->master));
      f->slave=open(ptsname(f->master),O_RDWR|O_NOCTTY);CHECK(f->slave>=0 && !tcgetattr(f->slave,&f->before));f->flags=fcntl(f->slave,F_GETFL);
      f->saved_in=dup(0);CHECK(f->saved_in>=0 && dup2(f->slave,0)==0);
    }
  }
  CHECK(!record_terminal(f,"before"));return 0;
}
static int restored_stdio(mixed_stdio *f) {
  if(f->terminal) {
    if(IsWindows()) { uint32_t mode;CHECK(GetConsoleMode(f->console,&mode) && mode==f->mode); }
    else { struct termios after;CHECK(!tcgetattr(f->slave,&after) && same_mode(&f->before,&after) && fcntl(f->slave,F_GETFL)==f->flags); }
  }
  CHECK(!record_terminal(f,"restored"));
  /* Host original remains usable after the SDK duplicate has retired. */
  CHECK(write(f->file,"H",1)==1);
  if(IsWindows()) { CHECK(SetStdHandle(-11,f->saved_out_handle) && CloseHandle(f->file_handle));if(f->terminal)CHECK(SetStdHandle(-10,f->saved_in_handle) && CloseHandle(f->console)); }
  else { CHECK(dup2(f->saved_out,1)==1 && !close(f->saved_out));if(f->terminal)CHECK(dup2(f->saved_in,0)==0 && !close(f->saved_in) && !close(f->slave) && !close(f->master)); }
  CHECK(!close(f->file) && !unlink(f->path));return 0;
}

static int stop_mixed(const char *self,const char *loader,const char *native_child,int terminal,int nonreturning,int races) {
  mixed_gate g=MIXED_GATE_INIT;unsigned notices=0;
  mixed_binding *binding=calloc(1,sizeof(*binding));CHECK(binding);
  int binding_fd=open("./mixed-binding",O_RDWR|O_CREAT|O_TRUNC,0600);CHECK(binding_fd>=0);
  binding->resource=fdopen(binding_fd,"w+b");CHECK(binding->resource);
  mixed_stdio fixture;CHECK(!prepare_stdio(&fixture,terminal));
  qaplet_config b=QAPLET_CONFIG_INIT;b.limits.event_queue_slots=1;
  qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.worker_threads=1;
  /* Real Windows process/file/Console helpers share one finite quota. This
   * deliberate combination needs more than the default four helpers. */
  m.limits.helper_threads=6;
  qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && !register_mixed(r,&g,binding));
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=MIXED_NOTIFY;hook.userdata=&notices;
  CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK);
  qaplet_io_endpoint stdio=QAPLET_IO_ENDPOINT_INIT,tty=QAPLET_IO_ENDPOINT_INIT;
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
  CHECK(qaplet_stdio_attach(r,1,&stdio,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_FILE);
  if(terminal) {
    qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;
    qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;
    CHECK(qaplet_tty_acquire(r,0,&options,&tty,&status,NULL)==QAPLET_OK && status.active);
    if(IsWindows()) { uint32_t mode;CHECK(GetConsoleMode(fixture.console,&mode) && mode!=fixture.mode); }
    else { struct termios mode;CHECK(!tcgetattr(fixture.slave,&mode) && !same_mode(&mode,&fixture.before)); }
    CHECK(!record_terminal(&fixture,"modified"));
  }
  CHECK(!evaluate(r,"Promise.all([import('qaplet:work'),import('qaplet:timers'),import('qaplet:events'),import('host:mixed')]).then(([w,t,e,h])=>{Object.assign(globalThis,{w,t,e,h});h.mark();return 42})"));
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && drive.phase==QAPLET_PHASE_DONE);
  qaplet_process_options o=QAPLET_PROCESS_OPTIONS_INIT;
  const char *path=IsWindows() ? native_child : loader;
  qaplet_string args[3]={{path,strlen(path)},{self,strlen(self)},{"--m2-mixed-child",16}};
  if(IsWindows()) { args[1]=(qaplet_string){"--hang",6};o.argc=2; } else o.argc=3;
  o.path=(qaplet_string){path,strlen(path)};o.argv=args;
  qaplet_process child=QAPLET_PROCESS_INIT;qaplet_process_status child_state=QAPLET_PROCESS_STATUS_INIT;
  CHECK(qaplet_process_spawn(r,&o,&child,&child_state,NULL)==QAPLET_OK);
  qaplet_io_endpoint out=QAPLET_IO_ENDPOINT_INIT;CHECK(qaplet_process_get_stdio(r,&child,1,&out,NULL)==QAPLET_OK);
  qaplet_io_operation read=QAPLET_IO_OPERATION_INIT;CHECK(qaplet_io_read(r,&out,1,&read,NULL)==QAPLET_OK);
  if(races)CHECK(!mixed_races(r,&g));
  uint64_t prior_cancelled=races ? 1 : 0;
  CHECK(!evaluate(r,"for(let i=0;i<2;++i)w.submit('held',new Uint8Array([17])).promise.then(()=>h.forbidden(),()=>h.forbidden()).finally(()=>h.forbidden());e.onEvent(()=>h.forbidden());t.setTimeout(()=>h.forbidden(),60000);42"));
  wait_entered(&g);
  qaplet_io_operation file_write=QAPLET_IO_OPERATION_INIT,tty_read=QAPLET_IO_OPERATION_INIT;
  CHECK(qaplet_io_write(r,&stdio,"S",1,&file_write,NULL)==QAPLET_OK);
  if(terminal)CHECK(qaplet_io_read(r,&tty,1,&tty_read,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(r,1,0,NULL,0,NULL)==QAPLET_OK && qaplet_post_event(r,2,0,NULL,0,NULL)==QAPLET_QUEUE_FULL);
  stop_request s={r,-1};pthread_t producer;CHECK(!pthread_create(&producer,NULL,request_stop,&s) && !pthread_join(producer,NULL) && s.code==QAPLET_OK);
  qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;qaplet_m2_snapshot state=QAPLET_M2_SNAPSHOT_INIT;
  qaplet_timer_snapshot timers=QAPLET_TIMER_SNAPSHOT_INIT;
  CHECK(qaplet_get_m2_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_STOP_REQUESTED);
  /* A running handler can observe the published stop cooperatively. This does
   * not cancel queued records or start owner resource shutdown. */
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.running==1 && work.queued==1 && work.cancelled==prior_cancelled && probe_cancel(&g));
  CHECK(qaplet_get_process_status(r,&child,&child_state,NULL)==QAPLET_OK && !child_state.exited && !child_state.code);
  CHECK(qaplet_get_timer_snapshot(r,&timers,NULL)==QAPLET_OK && timers.current==1 && !forbidden_calls);
  qaplet_io_status io_state=QAPLET_IO_STATUS_INIT;
  CHECK(qaplet_get_io_status(r,&file_write,&io_state,NULL)==QAPLET_OK && io_state.state==QAPLET_IO_PENDING);
  if(terminal) { qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;CHECK(qaplet_get_tty_status(r,&tty,&status,NULL)==QAPLET_OK && status.active && !status.restored); }
  MIXED_FAULT_STAGE(1,r);
  CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED && drive.reason==QAPLET_DRIVE_STOPPED);
  CHECK(qaplet_get_m2_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_STOPPED);
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.cancelled==prior_cancelled+2 && !work.queued && probe_cancel(&g));
  CHECK(qaplet_get_timer_snapshot(r,&timers,NULL)==QAPLET_OK && !timers.current);
  double until=milliseconds()+5000;
  do { CHECK(qaplet_process_get_status(r,&child,&child_state,NULL)==QAPLET_OK);if(child_state.reaped)break; } while(milliseconds()<until);
  CHECK(child_state.reaped && child_state.exited && child_state.code==QAPLET_CANCELLED && !forbidden_calls);
  MIXED_FAULT_STAGE(2,r);
  CHECK(qaplet_close(r,NULL)==MIXED_CLEANUP_CODE && !forbidden_calls && binding->calls==1);
  /* Old synchronous Host binding storage is no longer needed after close.
   * Its independent resource/data are actually freed while work still uses g. */
  CHECK(!fflush(binding->resource) && ftell(binding->resource)==1 && !fclose(binding->resource) && !unlink("./mixed-binding"));free(binding);
  CHECK(qaplet_get_process_status(r,&child,&child_state,NULL)==QAPLET_INVALID_HANDLE && qaplet_get_io_status(r,&read,&io_state,NULL)==QAPLET_INVALID_HANDLE && qaplet_get_io_status(r,&file_write,&io_state,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(qaplet_get_m2_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_CLOSED && !state.engine_current_bytes && state.native_state==QAPLET_NATIVE_CLEANING);
  for(unsigned i=0;i<3;++i) {
    double start=milliseconds();qaplet_code code=qaplet_quiesce(r,i ? 5 : 0,&drive,NULL);double elapsed=milliseconds()-start;
    FILE *out=fopen("mixed-waits.ndjson","ab");CHECK(out);fprintf(out,"{\"requested_active_wait_ms\":%u,\"observed_call_ms\":%.6f,\"code\":%u,\"reason\":%u}\n",i ? 5 : 0,elapsed,code,drive.reason);CHECK(!fclose(out));
    CHECK(code==MIXED_CLEANUP_CODE && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT && qaplet_destroy(r,NULL)==QAPLET_BUSY);
  }
  qaplet_process_snapshot processes=QAPLET_PROCESS_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(r,&processes,NULL)==QAPLET_OK && !processes.processes && !processes.helper_threads);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;
  CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.operations && !io.endpoints && !io.helper_threads && !io.data_bytes);
  CHECK(!restored_stdio(&fixture));
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.running==1 && work.operations==1 && work.data_bytes==2);
  if(nonreturning) {
    CHECK(!mixed_success("M2 mixed nonreturning: work remains physically held at process exit; CLOSED engine zero; repeated zero/finite quiesce pending and destroy BUSY; child reaped, stdio/TTY restored and other IO retired; no native cleanup success claimed: pass"));
    fflush(stdout);exit(0); /* Host supervisor ends the fixture, not SDK cleanup. */
  }
  release_work(&g);MIXED_FAULT_STAGE(3,r);
  if(MIXED_UNKNOWN) {
    /* A retained cleanup error deliberately ends active waiting. Repeated
     * public zero-wait collection still joins every independently safe worker. */
    double until=milliseconds()+5000;
    do { CHECK(qaplet_quiesce(r,0,&drive,NULL)==MIXED_CLEANUP_CODE && qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK);if(!work.worker_threads)break; }while(milliseconds()<until);
    CHECK(!work.worker_threads && !work.operations && !work.data_bytes);
  }
  CHECK(qaplet_quiesce(r,5000,&drive,NULL)==MIXED_CLEANUP_CODE && drive.reason==(MIXED_UNKNOWN ? QAPLET_DRIVE_WAIT_TIMEOUT : QAPLET_DRIVE_QUIESCENT));
  CHECK(qaplet_get_m2_snapshot(r,&state,NULL)==QAPLET_OK && state.lifecycle==QAPLET_CLOSED && !state.native_handles && state.native_uncertain_handles==MIXED_UNKNOWN && !state.reactor_registrations);
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && !work.operations && !work.worker_threads && !work.data_bytes && g.entered==1 && g.exited==1 && g.cancelled && !forbidden_calls && notices);
  /* All future producers were withdrawn and joined above. Host-owned binding,
   * work and notify storage is released only after its respective barrier. */
  MIXED_FAULT_STAGE(4,r);
  if(MIXED_UNKNOWN) {
    CHECK(qaplet_destroy(r,NULL)==QAPLET_BUSY);
    return mixed_success("M2 mixed uncertain close: stop remains first cause across CLOSED; replacement identity survives repeated cleanup; unknown handle retains BUSY; child/stdio/TTY/IO and work actually retire independently; no cleanup success claimed: pass");
  }
  CHECK(qaplet_destroy(r,NULL)==QAPLET_OK);g.magic=0;
  CHECK(!pthread_cond_destroy(&g.condition) && !pthread_mutex_destroy(&g.mutex));
  return mixed_success("M2 mixed stop: DONE/IDLE distinct; full queue stop only publishes; owner cancels work/timer/IO and force-reaps child before close; no user jobs; CLOSED retains work through retry/BUSY; other resources retire; actual quiescence and joined producers: pass");
}
int sdk_mixed(int argc,char **argv) {
  if(argc==2 && !strcmp(argv[1],"--m2-mixed-child")) { char byte;while(read(0,&byte,1)<0 && errno==EINTR){};return 0; }
  if(argc!=5)return 2;
  const char *modes[]={"--m2-mixed-stop","--m2-mixed-tty","--m2-mixed-races","--m2-mixed-tty-races","--m2-mixed-nonreturning","--m2-mixed-tty-nonreturning","--m2-mixed-notify","--m2-mixed-tty-notify","--m2-mixed-uncertain","--m2-mixed-tty-uncertain","--m2-mixed-initialization","--m2-mixed-wait-budget"};
  unsigned i;for(i=0;i<sizeof(modes)/sizeof(*modes);++i)if(!strcmp(argv[1],modes[i]))break;
  if(i==sizeof(modes)/sizeof(*modes))return 2;
  return stop_mixed(argv[2],argv[3],argv[4],!strcmp(argv[1],"--m2-mixed-tty") || strstr(argv[1],"tty-")!=NULL,!strcmp(argv[1],"--m2-mixed-nonreturning") || !strcmp(argv[1],"--m2-mixed-tty-nonreturning"),strstr(argv[1],"races")!=NULL);
}
#ifdef SDK_MIXED_STANDALONE
int main(int argc,char **argv) { return sdk_mixed(argc,argv); }
#endif
