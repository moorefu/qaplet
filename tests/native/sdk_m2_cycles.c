/* SPDX-License-Identifier: MIT */
/* Independently compiled fixture. application-m2.c is the exact sealed public
 * package example, not a checkout/private implementation. Reuse its bounded
 * file worker, copied event producer, byte-pipe collector and native child. */
#define main sealed_example_main
#include "application-m2.c"
#undef main
#include <termios.h>
#include <dirent.h>
#include "libc/nt/console.h"
#include "libc/nt/dll.h"

#define CYCLE(c) do { if (!(c)) { fprintf(stderr,"cycles line %d: %s errno=%d\n",__LINE__,#c,errno); return 1; } } while (0)
typedef struct {
  uint64_t completed, failed, created, closed, quiescent, destroyed, baseline_checks;
  uint64_t work, events, timer, stdio, tty, child_reaped, child_eof, producer_joined;
  uint64_t normal, cancelled, errors, generation_checks, last_generation;
  uint64_t phase_cycles[3], idle_wait_calls, idle_wait_requested_ms;
  double phase_elapsed_ms[3], idle_wait_elapsed_ms, start;
  uint64_t baseline_handles, baseline_helpers;
  uint64_t baseline_workers;
  int baseline_frozen;
  uint64_t os_handles_before,os_threads_before,os_handles_after,os_threads_after;
  uint64_t os_initial_handles,os_initial_threads,warmup_instances;
  const char *mode, *phase;
  uint64_t requested;
  FILE *progress;
} cycles;
static cycles totals;
static unsigned native_pid(void) {
  unsigned pid=getpid();
#if defined(__x86_64__)
  if(IsWindows()) {
    int64_t kernel=LoadLibraryA("kernel32.dll");
    uint32_t (__msabi *query)(void)=(void *)GetProcAddress(kernel,"GetCurrentProcessId");
    if(query)pid=query();
  }
#endif
  return pid;
}
static unsigned native_tid(void) {
#if defined(__x86_64__)
  if(IsWindows()) {
    int64_t kernel=LoadLibraryA("kernel32.dll");
    uint32_t (__msabi *query)(void)=(void *)GetProcAddress(kernel,"GetCurrentThreadId");
    if(query)return query();
  }
#endif
  return getpid(); /* The fixture owner is the process main thread. */
}
static int os_resources(uint64_t *handles,uint64_t *threads) {
  *handles=*threads=0;
#if defined(__x86_64__)
  if(IsWindows()) {
    int64_t kernel=LoadLibraryA("kernel32.dll");
    int32_t (__msabi *get_handles)(int64_t,uint32_t *)=(void *)GetProcAddress(kernel,"GetProcessHandleCount");
    int64_t (__msabi *snapshot_threads)(uint32_t,uint32_t)=(void *)GetProcAddress(kernel,"CreateToolhelp32Snapshot");
    int32_t (__msabi *first)(int64_t,void *)=(void *)GetProcAddress(kernel,"Thread32First");
    int32_t (__msabi *next)(int64_t,void *)=(void *)GetProcAddress(kernel,"Thread32Next");
    CYCLE(get_handles && snapshot_threads && first && next);
    struct { uint32_t size,usage,id,owner;int32_t base,delta;uint32_t flags; } entry={0};
    entry.size=sizeof(entry);int64_t snap=snapshot_threads(4,0);CYCLE(snap!=-1);
    int more=first(snap,&entry);CYCLE(more);
    while(more) { if(entry.owner==native_pid())++*threads;entry.size=sizeof(entry);more=next(snap,&entry); }
    CYCLE(CloseHandle(snap));uint32_t count=0;CYCLE(get_handles(GetCurrentProcess(),&count));*handles=count;
    return 0;
  }
#endif
  if(IsLinux()) {
    const char *paths[]={"/proc/self/fd","/proc/self/task"};uint64_t *counts[]={handles,threads};
    for(unsigned i=0;i<2;++i) {
      DIR *dir=opendir(paths[i]);CYCLE(dir);struct dirent *entry;
      while((entry=readdir(dir)))if(strcmp(entry->d_name,".") && strcmp(entry->d_name,".."))++*counts[i];
      CYCLE(!closedir(dir));
    }
    return 0;
  }
  return 0; /* Other native targets need their explicit OS observer. */
}

static int record(const char *type) {
  char line[4096];
  int n=snprintf(line,sizeof(line),
    "{\"type\":\"%s\",\"mode\":\"%s\",\"phase\":\"%s\",\"process_id\":%u,\"owner_thread_id\":%u,"
    "\"requested\":%" PRIu64 ",\"completed\":%" PRIu64 ",\"failed\":%" PRIu64 ",\"elapsed_ms\":%.6f,"
    "\"created\":%" PRIu64 ",\"runtime_instances\":%" PRIu64 ",\"closed\":%" PRIu64 ",\"quiescent\":%" PRIu64 ",\"destroyed\":%" PRIu64 ","
    "\"native_cleanup_complete\":%s,\"baseline_checks\":%" PRIu64 ",\"work\":%" PRIu64 ",\"events\":%" PRIu64 ",\"timer\":%" PRIu64 ","
    "\"stdio\":%" PRIu64 ",\"tty\":%" PRIu64 ",\"child_reaped\":%" PRIu64 ",\"child_eof\":%" PRIu64 ",\"producer_joined\":%" PRIu64 ","
    "\"normal\":%" PRIu64 ",\"cancelled\":%" PRIu64 ",\"errors\":%" PRIu64 ",\"generation_checks\":%" PRIu64 ","
    "\"phase_elapsed_ms\":{\"idle\":%.6f,\"load\":%.6f,\"cancel\":%.6f},"
    "\"phase_cycles\":{\"idle\":%" PRIu64 ",\"load\":%" PRIu64 ",\"cancel\":%" PRIu64 "},"
    "\"idle_wait_calls\":%" PRIu64 ",\"idle_wait_requested_ms\":%" PRIu64 ",\"idle_wait_elapsed_ms\":%.6f,"
    "\"baseline_operations\":0,\"baseline_data_bytes\":0,\"baseline_timers\":0,\"baseline_processes\":0,\"baseline_io_endpoints\":0,"
    "\"baseline_handles\":%" PRIu64 ",\"baseline_helpers\":%" PRIu64 ",\"baseline_workers\":%" PRIu64 ",\"baseline_frozen\":%s,\"helper_limit\":4,"
    "\"os_resource_source\":\"%s\",\"os_handles_before\":%" PRIu64 ",\"os_threads_before\":%" PRIu64 ",\"os_handles_after\":%" PRIu64 ",\"os_threads_after\":%" PRIu64 ","
    "\"os_initial_handles\":%" PRIu64 ",\"os_initial_threads\":%" PRIu64 ",\"warmup_instances\":%" PRIu64 "}\n",
    type,totals.mode,totals.phase,native_pid(),native_tid(),totals.requested,totals.completed,totals.failed,milliseconds()-totals.start,
    totals.created,totals.created,totals.closed,totals.quiescent,totals.destroyed,
    totals.created==totals.destroyed ? "true":"false",totals.baseline_checks,totals.work,totals.events,totals.timer,
    totals.stdio,totals.tty,totals.child_reaped,totals.child_eof,totals.producer_joined,totals.normal,totals.cancelled,totals.errors,totals.generation_checks,
    totals.phase_elapsed_ms[0],totals.phase_elapsed_ms[1],totals.phase_elapsed_ms[2],
    totals.phase_cycles[0],totals.phase_cycles[1],totals.phase_cycles[2],
    totals.idle_wait_calls,totals.idle_wait_requested_ms,totals.idle_wait_elapsed_ms,totals.baseline_handles,totals.baseline_helpers,
    totals.baseline_workers,totals.baseline_frozen ? "true":"false",
    IsWindows() ? "GetProcessHandleCount+Toolhelp32Threads":IsLinux() ? "/proc/self/fd+/proc/self/task":"not_measured",
    totals.os_handles_before,totals.os_threads_before,totals.os_handles_after,totals.os_threads_after,
    totals.os_initial_handles,totals.os_initial_threads,totals.warmup_instances);
  CYCLE(n>0 && (size_t)n<sizeof(line));
  CYCLE(fwrite(line,1,n,totals.progress)==(size_t)n && !fflush(totals.progress));
  CYCLE(fwrite(line,1,n,stdout)==(size_t)n && !fflush(stdout));
  return 0;
}

static int evaluate_cycles(qaplet_runtime *r,const char *source) {
  qaplet_progress p=QAPLET_PROGRESS_INIT;
  CYCLE(qaplet_eval(r,source,strlen(source),&p,NULL)==QAPLET_OK);
  double until=milliseconds()+5000;
  while(p.phase==QAPLET_PHASE_PENDING && milliseconds()<until)
    CYCLE(qaplet_pump(r,&p,NULL)==QAPLET_OK);
  CYCLE(p.phase==QAPLET_PHASE_DONE);
  return 0;
}

static int baseline(application *app,int closed) {
  qaplet_resource_snapshot r=QAPLET_RESOURCE_SNAPSHOT_INIT;
  qaplet_m2_snapshot m=QAPLET_M2_SNAPSHOT_INIT;
  qaplet_timer_snapshot t=QAPLET_TIMER_SNAPSHOT_INIT;
  qaplet_process_snapshot p=QAPLET_PROCESS_SNAPSHOT_INIT;
  qaplet_m2_diagnostic d=QAPLET_M2_DIAGNOSTIC_INIT;
  qaplet_work_snapshot w=QAPLET_WORK_SNAPSHOT_INIT;
  CYCLE(qaplet_get_resource_snapshot(app->runtime,&r,NULL)==QAPLET_OK &&
        qaplet_get_m2_snapshot(app->runtime,&m,NULL)==QAPLET_OK &&
        qaplet_get_timer_snapshot(app->runtime,&t,NULL)==QAPLET_OK &&
        qaplet_get_process_snapshot(app->runtime,&p,NULL)==QAPLET_OK &&
        qaplet_get_m2_diagnostic(app->runtime,&d,NULL)==QAPLET_OK &&
        qaplet_get_work_snapshot(app->runtime,&w,NULL)==QAPLET_OK);
  CYCLE(!r.operations && !r.operation_data_bytes && !r.io_endpoints && !t.current && !p.processes);
  CYCLE(!m.native_uncertain_handles && !d.cleanup_reason && !d.control_error && !m.termination_reason);
  CYCLE(m.m2_limits.helper_threads==4 && r.helper_threads<=4);
  if(closed) CYCLE(m.lifecycle==QAPLET_CLOSED && m.native_state==QAPLET_NATIVE_QUIESCENT &&
                   !m.native_handles && !m.reactor_registrations && !r.helper_threads && !r.helper_stack_bytes);
  else CYCLE(m.lifecycle==QAPLET_OPEN);
  if(!closed && totals.baseline_frozen) CYCLE(totals.baseline_handles==m.native_handles &&
    totals.baseline_helpers==r.helper_threads && totals.baseline_workers==w.worker_threads);
  if(!closed && !totals.baseline_frozen) {
    totals.baseline_handles=m.native_handles;totals.baseline_helpers=r.helper_threads;totals.baseline_workers=w.worker_threads;
  }
  ++totals.baseline_checks;
  return 0;
}

static int create_cycles(application *app) {
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  CYCLE(qaplet_create_m2(&b,&m,&app->runtime,NULL)==QAPLET_OK);++totals.created;
  return 0;
}
static int destroy_cycles(application *app) {
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CYCLE(qaplet_close(app->runtime,NULL)==QAPLET_OK);++totals.closed;
  CYCLE(qaplet_quiesce(app->runtime,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  ++totals.quiescent;CYCLE(!baseline(app,1));
  CYCLE(qaplet_destroy(app->runtime,NULL)==QAPLET_OK);++totals.destroyed;app->runtime=NULL;
  return 0;
}

static int setup(application *app) {
  memset(app,0,sizeof(*app));app->receive=app->transmit=-1;
  CYCLE(!pthread_mutex_init(&app->gate_mutex,NULL) && !pthread_cond_init(&app->gate_condition,NULL));
  app->gate_initialized=1;
  CYCLE(open_channel(app) && !create_cycles(app));
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=notify;hook.userdata=app;
  CYCLE(qaplet_set_host_notify(app->runtime,&hook,NULL)==QAPLET_OK);
  qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;
  handler.name="read-file-prefix";handler.name_length=16;handler.function=read_file;handler.userdata=app;
  handler.input_bytes=PATH_BYTES;handler.output_bytes=FILE_BYTES;
  CYCLE(qaplet_register_work_handler(app->runtime,&handler,NULL)==QAPLET_OK);
  CYCLE(!evaluate_cycles(app->runtime,
    "Promise.all([import('qaplet:events'),import('qaplet:timers')]).then(([e,t])=>{"
    "globalThis.cycleTimers=t;globalThis.eventCount=0;globalThis.eventCopied=false;"
    "e.onEvent(v=>{eventCopied=v.type===23&&v.token===42n&&v.payload.length===3&&"
    "v.payload[0]===0&&v.payload[1]===255&&v.payload[2]===17;eventCount++});return 42})"));
  return 0;
}

static int teardown(application *app) {
  CYCLE(!destroy_cycles(app));
  CYCLE(!close(app->receive) && !close(app->transmit));
  CYCLE(!pthread_cond_destroy(&app->gate_condition) && !pthread_mutex_destroy(&app->gate_mutex));
  return 0;
}

static int io_complete(application *app,qaplet_io_operation *op,qaplet_io_result *result) {
  double until=milliseconds()+5000;
  for(;;) {
    qaplet_io_status s=QAPLET_IO_STATUS_INIT;
    CYCLE(qaplet_get_io_status(app->runtime,op,&s,NULL)==QAPLET_OK);
    if(s.state!=QAPLET_IO_PENDING && !s.physical_pending && !s.pending_handoff && !s.helper_active)break;
    qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;
    CYCLE(qaplet_run_once(app->runtime,100,&d,NULL)==QAPLET_OK && milliseconds()<until);
  }
  CYCLE(qaplet_io_copy_result(app->runtime,op,result,NULL)==QAPLET_OK);
  CYCLE(qaplet_io_release(app->runtime,op,NULL)==QAPLET_OK);
  return 0;
}

/* Borrow an actual current stdout regular file, preserve its Host descriptor,
 * and exercise a private POSIX PTY / inherited Windows Console. File IO and
 * Console IO run after child helpers retire, staying inside default helper4. */
static int standard_routes(application *app) {
  char path[]="./cycles-stdio-XXXXXX";int fd=mkstemp(path);CYCLE(fd>=0 && !fflush(stdout));
  int saved=-1;if(!IsWindows()) { saved=dup(1);CYCLE(saved>=0 && dup2(fd,1)==1); }
  int64_t old_handle=0, file_handle=0;
  if(IsWindows()) {
    extern int __mkntpath(const char *,char16_t *);
    extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
    char16_t native[4096];CYCLE(__mkntpath(path,native)>=0);
    file_handle=CreateFile(native,0xc0000000u,7,NULL,3,0x80,0);
    old_handle=GetStdHandle(-11);CYCLE(file_handle!=-1 && SetStdHandle(-11,file_handle));
  }
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;
  CYCLE(qaplet_stdio_attach(app->runtime,1,&endpoint,&info,NULL)==QAPLET_OK && info.kind==QAPLET_STDIO_FILE);
  qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;
  CYCLE(qaplet_io_write(app->runtime,&endpoint,"S",1,&op,NULL)==QAPLET_OK);
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;
  CYCLE(!io_complete(app,&op,&result) && !result.code && result.length==1);
  CYCLE(qaplet_io_close(app->runtime,&endpoint,NULL)==QAPLET_OK);
  CYCLE(qaplet_stdio_detach(app->runtime,1,NULL)==QAPLET_OK);
  CYCLE(write(fd,"H",1)==1);
  if(IsWindows())CYCLE(SetStdHandle(-11,old_handle) && CloseHandle(file_handle));
  if(!IsWindows())CYCLE(dup2(saved,1)==1 && !close(saved));
  CYCLE(!close(fd) && !unlink(path));++totals.stdio;

  int master=-1,slave=-1,saved_input=-1,flags=0;
  int64_t console=0,old_input=0;uint32_t before_mode=0;struct termios before,after;
  if(IsWindows()) {
    extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
    /* Host fixture explicitly opens its attached ConPTY input and installs it
     * as the actual standard route. The SDK never discovers a hidden route. */
    console=CreateFile(u"CONIN$",0xc0000000u,3,NULL,3,0,0);old_input=GetStdHandle(-10);
    CYCLE(console!=-1 && GetConsoleMode(console,&before_mode) && SetStdHandle(-10,console));
  }
  else {
    master=posix_openpt(O_RDWR|O_NOCTTY);CYCLE(master>=0 && !grantpt(master) && !unlockpt(master));
    slave=open(ptsname(master),O_RDWR|O_NOCTTY);CYCLE(slave>=0 && !tcgetattr(slave,&before));
    flags=fcntl(slave,F_GETFL);saved_input=dup(0);CYCLE(saved_input>=0 && dup2(slave,0)==0);
  }
  qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;options.flags=QAPLET_TTY_RAW;
  qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;qaplet_io_endpoint tty=QAPLET_IO_ENDPOINT_INIT;
  CYCLE(qaplet_tty_acquire(app->runtime,0,&options,&tty,&status,NULL)==QAPLET_OK && status.active);
  if(IsWindows()) { uint32_t mode;CYCLE(GetConsoleMode(console,&mode) && mode!=before_mode); }
  else CYCLE(!tcgetattr(slave,&after) && memcmp(&before,&after,sizeof(before)));
  /* A real pending read is cancelled. The operation is collected and released
   * only after native completion and actual helper join, then mode is restored. */
  op=(qaplet_io_operation)QAPLET_IO_OPERATION_INIT;
  CYCLE(qaplet_io_read(app->runtime,&tty,1,&op,NULL)==QAPLET_OK);
  CYCLE(qaplet_io_cancel(app->runtime,&op,NULL)==QAPLET_OK);
  unsigned char byte;result=(qaplet_io_result)QAPLET_IO_RESULT_INIT;result.buffer=&byte;result.capacity=1;
  CYCLE(!io_complete(app,&op,&result) && result.code==QAPLET_CANCELLED);
  double until=milliseconds()+5000;
  for(;;) {
    qaplet_code code=qaplet_tty_release(app->runtime,&tty,&status,NULL);
    if(code==QAPLET_OK)break;
    CYCLE(code==QAPLET_BUSY && milliseconds()<until);
    qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;CYCLE(qaplet_run_once(app->runtime,100,&d,NULL)==QAPLET_OK);
  }
  CYCLE(qaplet_stdio_detach(app->runtime,0,NULL)==QAPLET_OK);
  if(IsWindows()) { uint32_t mode;CYCLE(GetConsoleMode(console,&mode) && mode==before_mode && SetStdHandle(-10,old_input) && CloseHandle(console)); }
  else {
    CYCLE(!tcgetattr(slave,&after));
    CYCLE(before.c_iflag==after.c_iflag && before.c_oflag==after.c_oflag && before.c_cflag==after.c_cflag &&
      before.c_lflag==after.c_lflag && !memcmp(before.c_cc,after.c_cc,sizeof(before.c_cc)) &&
      cfgetispeed(&before)==cfgetispeed(&after) && cfgetospeed(&before)==cfgetospeed(&after) && fcntl(slave,F_GETFL)==flags);
    CYCLE(dup2(saved_input,0)==0 && !close(saved_input) && !close(slave) && !close(master));
  }
  ++totals.tty;return 0;
}

static int mixed_cycle(application *app,unsigned kind,const char *self,const char *launcher,const char *file) {
  app->gated=kind==1;app->entered=app->released=0;app->work_done=app->input_started=app->writing=0;
  app->producer_started=app->producer_joined=0;app->written=0;
  memset(app->reading,0,sizeof(app->reading));memset(app->eof,0,sizeof(app->eof));
  memset(app->output_length,0,sizeof(app->output_length));app->work=(qaplet_work_result)QAPLET_WORK_RESULT_INIT;
  app->process=(qaplet_process)QAPLET_PROCESS_INIT;app->child=(qaplet_process_status)QAPLET_PROCESS_STATUS_INIT;
  CYCLE(!evaluate_cycles(app->runtime,
    "eventCount=0;eventCopied=false;globalThis.cycleTimerDone=false;"
    "cycleTimers.setTimeout(()=>{cycleTimerDone=true},1);"
    "var cancelled=cycleTimers.setTimeout(()=>{throw Error('cancelled timer ran')},60000);cycleTimers.clearTimeout(cancelled);42"));
  const char *input_file=kind==2 ? "./cycles-intentionally-missing-file" : file;
  CYCLE(qaplet_work_submit(app->runtime,"read-file-prefix",16,input_file,strlen(input_file)+1,&app->task,NULL)==QAPLET_OK);
  if(totals.last_generation) { CYCLE(app->task.generation>totals.last_generation);++totals.generation_checks; }
  totals.last_generation=app->task.generation;
  if(kind==1) {
    CYCLE(wait_entered(app) && qaplet_work_cancel(app->runtime,&app->task,NULL)==QAPLET_OK);
    qaplet_work_status s=QAPLET_WORK_STATUS_INIT;
    CYCLE(qaplet_get_work_status(app->runtime,&app->task,&s,NULL)==QAPLET_OK && s.code==QAPLET_CANCELLED && s.physical_pending);
    release_gate(app);
  }
  CYCLE(qaplet_keepalive_create(app->runtime,&app->keepalive,NULL)==QAPLET_OK);
  CYCLE(!pthread_create(&app->producer,NULL,produce,app));app->producer_started=1;
  qaplet_process_options options=QAPLET_PROCESS_OPTIONS_INIT;qaplet_string argv[3];size_t argc=0;
  int direct=!strcmp(launcher,"-");const char *executable=direct ? self:launcher;
  options.path=(qaplet_string){executable,strlen(executable)};argv[argc++]=options.path;
  if(!direct)argv[argc++]=(qaplet_string){self,strlen(self)};
  argv[argc++]=(qaplet_string){"--child",7};options.argv=argv;options.argc=argc;
  CYCLE(qaplet_process_spawn(app->runtime,&options,&app->process,&app->child,NULL)==QAPLET_OK);
  for(unsigned i=0;i<3;++i) {
    app->streams[i]=(qaplet_io_endpoint)QAPLET_IO_ENDPOINT_INIT;app->operations[i]=(qaplet_io_operation)QAPLET_IO_OPERATION_INIT;
    CYCLE(qaplet_process_get_stdio(app->runtime,&app->process,i,&app->streams[i],NULL)==QAPLET_OK);
  }
  /* The persistent Windows Console join controller occupies one helper.
   * Start stderr now; retire the finite stdin write before starting stdout.
   * The child echoes only this bounded five-byte input, so its OS pipe holds
   * that prefix safely. Both output channels are then drained independently. */
  CYCLE(qaplet_io_read(app->runtime,&app->streams[2],FILE_BYTES,&app->operations[2],NULL)==QAPLET_OK);app->reading[1]=1;
  CYCLE(host_wait(app,3000)==1 && !pthread_join(app->producer,NULL));app->producer_joined=1;
  CYCLE(!atomic_load(&app->producer_error));++totals.producer_joined;
  double until=milliseconds()+5000;
  int stdout_started=0;
  for(;;) {
    CYCLE(host_wait(app,0)>=0 && !atomic_load(&app->notify_error));
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CYCLE(qaplet_run_once(app->runtime,100,&drive,NULL)==QAPLET_OK);
    CYCLE(!collect_work(app) && !input(app));
    if(app->input_started && !app->writing && !stdout_started) {
      CYCLE(qaplet_io_read(app->runtime,&app->streams[1],FILE_BYTES,&app->operations[1],NULL)==QAPLET_OK);
      app->reading[0]=1;stdout_started=1;
    }
    CYCLE(!collect_stream(app,0) && !collect_stream(app,1));
    CYCLE(qaplet_process_get_status(app->runtime,&app->process,&app->child,NULL)==QAPLET_OK);
    if(app->work_done && app->input_started && !app->writing && app->eof[0] && app->eof[1] &&
       app->child.reaped && app->child.delivered && !app->child.referenced)break;
    CYCLE(milliseconds()<until);
  }
  CYCLE(app->child.exited && !app->child.code && app->child.native_exit_code==37);
  CYCLE(app->output_length[1]==13 && !memcmp(app->output[1],"child stderr\n",13));
  if(!kind) { CYCLE(!app->work.code && app->work.length==5 && app->written==5 && app->output_length[0]==5 && !memcmp(app->output[0],"I\0\xffN\n",5));++totals.normal; }
  else if(kind==1) { CYCLE(app->work.code==QAPLET_CANCELLED && !app->work.length && !app->written && !app->output_length[0]);++totals.cancelled; }
  else { CYCLE(app->work.code==QAPLET_HOST_ERROR && app->work.error_domain==QAPLET_ERROR_DOMAIN_ERRNO && app->work.error==ENOENT && !app->written && !app->output_length[0]);++totals.errors; }
  ++totals.work;++totals.child_reaped;++totals.child_eof;
  CYCLE(qaplet_process_release(app->runtime,&app->process,NULL)==QAPLET_OK);
  CYCLE(!evaluate_cycles(app->runtime,"eventCount===1&&eventCopied&&cycleTimerDone"));
  qaplet_result result=QAPLET_RESULT_INIT;
  CYCLE(qaplet_get_result(app->runtime,&result,NULL)==QAPLET_OK && result.type==QAPLET_VALUE_BOOL && result.boolean);
  ++totals.events;++totals.timer;
  CYCLE(!standard_routes(app));
  /* Retire deferred endpoint-close handoff, then query the public baseline. */
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;CYCLE(qaplet_run_once(app->runtime,0,&d,NULL)==QAPLET_OK);
  CYCLE(!baseline(app,0));return 0;
}

static int idle_phase(application *app) {
  CYCLE(!evaluate_cycles(app->runtime,"globalThis.idleDone=false;cycleTimers.setTimeout(()=>{idleDone=true},1000);42"));
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;
  double until=milliseconds()+5000;
  qaplet_timer_snapshot timer=QAPLET_TIMER_SNAPSHOT_INIT;
  do {
    double start=milliseconds();
    CYCLE(qaplet_run_once(app->runtime,2000,&d,NULL)==QAPLET_OK);
    double elapsed=milliseconds()-start;
    totals.idle_wait_elapsed_ms+=elapsed;++totals.idle_wait_calls;totals.idle_wait_requested_ms+=2000;
    FILE *waits=fopen("cycles-waits.ndjson","ab");CYCLE(waits);
    CYCLE(fprintf(waits,"{\"phase\":\"idle\",\"index\":%" PRIu64 ",\"requested_ms\":2000,\"actual_monotonic_elapsed_ms\":%.6f,\"measurement\":\"whole qaplet_run_once call; not timer lateness\"}\n",totals.idle_wait_calls,elapsed)>0 && !fclose(waits));
    CYCLE(qaplet_get_timer_snapshot(app->runtime,&timer,NULL)==QAPLET_OK && milliseconds()<until);
  } while(timer.current);
  CYCLE(!evaluate_cycles(app->runtime,"idleDone"));qaplet_result r=QAPLET_RESULT_INIT;
  CYCLE(qaplet_get_result(app->runtime,&r,NULL)==QAPLET_OK && r.type==QAPLET_VALUE_BOOL && r.boolean);
  CYCLE(!baseline(app,0));return 0;
}

int main(int argc,char **argv) {
  if(argc==2 && !strcmp(argv[1],"--child"))return finish(child());
  if(argc!=3 && argc!=6)return finish(2);
  int lifecycle=!strcmp(argv[1],"--lifecycle"),soak=!strcmp(argv[1],"--soak");
  if(!lifecycle && !soak && strcmp(argv[1],"--mixed"))return finish(2);
  if((lifecycle && argc!=3) || (!lifecycle && argc!=6))return finish(2);
  char *end;errno=0;unsigned long long requested=strtoull(argv[2],&end,10);
  if(errno || *end || !requested || argv[2][0]=='-')return finish(2);
  totals.mode=lifecycle ? "lifecycle":soak ? "soak":"mixed";totals.phase="initializing";totals.requested=requested;
  totals.start=milliseconds();totals.progress=fopen("cycles-progress.ndjson","wb");if(!totals.progress)return finish(1);
  int code=os_resources(&totals.os_handles_before,&totals.os_threads_before);
  if(!code)code=record("ready");double last_progress=totals.start;
  application app;
  /* Three separate, fully destroyed warmup instances expose one-time CRT/OS
   * initialization. Preserve initial and warmup values, then freeze the OS
   * baseline once. These cycles never count toward the requested result. */
  cycles initial=totals;
  for(unsigned k=0;!code && k<3;++k) {
    if(lifecycle) { memset(&app,0,sizeof(app));code=create_cycles(&app);if(!code)code=destroy_cycles(&app); }
    else { totals.last_generation=0;code=setup(&app);if(!code)code=mixed_cycle(&app,k,argv[3],argv[4],argv[5]);if(!code)code=teardown(&app); }
  }
  if(!code) {
    CYCLE(totals.created==3 && totals.closed==3 && totals.quiescent==3 && totals.destroyed==3);
    totals=initial;totals.os_initial_handles=initial.os_handles_before;totals.os_initial_threads=initial.os_threads_before;
    totals.warmup_instances=3;
    code=os_resources(&totals.os_handles_before,&totals.os_threads_before);
    if(!code)code=record("os-baseline");
  }
  if(soak && !code) {
    code=setup(&app);totals.phase="warmup";
    for(unsigned k=0;!code && k<3;++k) { code=mixed_cycle(&app,k,argv[3],argv[4],argv[5]);if(!code)++totals.completed; }
    if(!code) { totals.baseline_frozen=1;code=record("baseline"); }
  }
  double measurement_start=milliseconds();
  for(uint64_t i=0;!code && (soak ? milliseconds()-measurement_start<requested*1000.0:i<requested);++i) {
    unsigned kind=i%3;
    if(soak) {
      unsigned phase=i%3;totals.phase=phase==0 ? "idle":phase==1 ? "load":"cancel";
      code=record("progress");double phase_start=milliseconds();
      if(!code && !phase) { do { code=idle_phase(&app); } while(!code && milliseconds()-phase_start<10000); }
      else if(!code) {
        double phase_until=phase_start+10000;
        do { code=mixed_cycle(&app,phase==2 ? 1:(totals.completed%2 ? 2:0),argv[3],argv[4],argv[5]);if(!code)++totals.completed; } while(!code && milliseconds()<phase_until);
      }
      totals.phase_elapsed_ms[phase]+=milliseconds()-phase_start;++totals.phase_cycles[phase];
    } else if(lifecycle) {
      memset(&app,0,sizeof(app));totals.phase="lifecycle";
      code=create_cycles(&app);if(!code)code=destroy_cycles(&app);if(!code)++totals.completed;
    } else {
      totals.phase=kind==1 ? "cancel":"load";totals.last_generation=0;
      code=setup(&app);if(!code)code=mixed_cycle(&app,kind,argv[3],argv[4],argv[5]);
      if(!code)code=teardown(&app);if(!code)++totals.completed;
    }
    if(!code && (milliseconds()-last_progress>=1000 || (!soak && (i+1==requested || (i+1)%100==0)))) { code=record("progress");last_progress=milliseconds(); }
  }
  if(soak && !code)code=teardown(&app);
  if(!code)code=os_resources(&totals.os_handles_after,&totals.os_threads_after);
  /* OS background threads may retire during a long run. Retain both actual
   * values and reject growth above the one frozen post-warmup baseline. */
  if(!code && (IsLinux() || IsWindows()))code=totals.os_handles_after>totals.os_handles_before || totals.os_threads_after>totals.os_threads_before;
  if(code)++totals.failed;
  totals.phase=code ? "failed":"finished";
  if(record("final"))code=1;
  if(fclose(totals.progress))code=1;
  return finish(code ? 1:0);
}
