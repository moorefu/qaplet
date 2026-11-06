/* SPDX-License-Identifier: MIT */
#define _COSMO_SOURCE 1
#include "qaplet.h"
/* Product-private drive boundary; this is not an Embed SDK API. It uses run's
 * automatic liveness rules and returns before the first independent delivery
 * can replace the completed entry result that the CLI must copy. */
qaplet_code qaplet_drive_entry(qaplet_runtime *, qaplet_drive_result *, qaplet_diagnostic *);
#include <inttypes.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "libc/dce.h"
#include "pipe_write.h"
#include "libc/nt/console.h"
#include "libc/nt/events.h"
#include "libc/nt/dll.h"
#include "libc/nt/ipc.h"
#include "libc/nt/runtime.h"
#include "libc/nt/synchronization.h"
#include "libc/nt/nt/file.h"
#include "libc/nt/struct/filemodeinformation.h"
extern uint32_t GetFileType(int64_t);
extern bool32 GetOverlappedResult(int64_t,struct NtOverlapped *,uint32_t *,bool32);
extern bool32 CancelIoEx(int64_t,struct NtOverlapped *);
extern bool32 DuplicateHandle(int64_t,int64_t,int64_t,int64_t *,uint32_t,bool32,uint32_t);

/* Process-owned interrupt facilities deliberately survive until process exit.
 * Removing a handler cannot join an entry which already copied its fd/HANDLE.
 * Keeping these two descriptors / two events and static state prevents reuse
 * by a late entry, including after Runtime destruction and during final output.
 * Neither asynchronous entry ever reads the Runtime pointer. */
static int interrupt_pipe[2]={-1,-1};
static int64_t interrupt_event,relay_event;
static unsigned interrupted,relay_shutdown,interrupt_error;
static pthread_t relay_thread;
static int64_t relay_native_handle;
static pthread_mutex_t relay_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t relay_condition;
static int relay_started,relay_done;
static qaplet_runtime *relay_runtime;
static uint64_t cleanup_remaining_ns=5000000000ull;
static int cleanup_clock_failed;
_Static_assert(__atomic_always_lock_free(4,0),"interrupt publication is lock free");

static int clock_ns(uint64_t *value){
  struct timespec now;
  if(clock_gettime(CLOCK_MONOTONIC,&now) || now.tv_sec<0 ||
      (uint64_t)now.tv_sec>(UINT64_MAX-(uint64_t)now.tv_nsec)/1000000000)return 0;
  *value=(uint64_t)now.tv_sec*1000000000+now.tv_nsec;return 1;
}
static uint64_t begin_native_wait(void){uint64_t now=0;if(!clock_ns(&now))cleanup_clock_failed=1;return now;}
static void end_native_wait(uint64_t before){
  uint64_t now;
  if(!clock_ns(&now) || now<before){cleanup_clock_failed=1;cleanup_remaining_ns=0;return;}
  uint64_t used=now-before;cleanup_remaining_ns=used>=cleanup_remaining_ns ? 0 : cleanup_remaining_ns-used;
}
static uint64_t remaining_ms(void){return cleanup_clock_failed ? 0 : cleanup_remaining_ns/1000000;}
static void unix_interrupt(int number){
  int saved=errno;unsigned char hint=1;(void)number;
  __atomic_store_n(&interrupted,1,__ATOMIC_SEQ_CST);
  /* The write side is permanently nonblocking. Full means a wake is already
   * queued. EINTR is retried without allocation, logging or any SDK operation. */
  ssize_t n;do{n=write(interrupt_pipe[1],&hint,1);}while(n<0 && errno==EINTR);
  if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)__atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);
  errno=saved;
}
#if defined(__x86_64__)
typedef bool32 (__msabi *ctrl_entry)(uint32_t);
static bool32 (__msabi *raw_set_event)(int64_t);
static bool32 (__msabi *raw_set_ctrl)(ctrl_entry,bool32);
static bool32 (__msabi *cancel_synchronous)(int64_t);
/* Foreign OS threads have no Cosmopolitan TLS. Only inline lock-free atomics
 * and pre-resolved raw Win32 MS-ABI calls are allowed on this path. */
static bool32 __msabi __attribute__((no_instrument_function,no_stack_protector)) windows_interrupt(uint32_t control){
  if(control!=0)return 0;
  __atomic_store_n(&interrupted,1,__ATOMIC_SEQ_CST);
  bool32 output_woken=raw_set_event(interrupt_event),relay_woken=raw_set_event(relay_event);
  if(!output_woken || !relay_woken)
    __atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);
  return 1;
}
#endif
static void *interrupt_relay(void *unused){
  (void)unused;
  if(IsWindows()){
    int64_t native=0;
    if(!DuplicateHandle(-1,-2,-1,&native,0,0,2)){
      __atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);goto done;
    }
    __atomic_store_n(&relay_native_handle,native,__ATOMIC_SEQ_CST);
  }
  for(;;){
    if(IsWindows()){
      if(WaitForSingleObject(relay_event,0xffffffff)!=0){__atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);break;}
    }else{
      unsigned char hints[128];ssize_t n;
      do{n=read(interrupt_pipe[0],hints,sizeof(hints));}while(n<0 && errno==EINTR);
      if(n<=0){__atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);break;}
    }
    if(__atomic_load_n(&relay_shutdown,__ATOMIC_SEQ_CST))break;
    if(__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST)){
      qaplet_runtime *runtime=__atomic_load_n(&relay_runtime,__ATOMIC_SEQ_CST);
      if(runtime && qaplet_request_stop(runtime,NULL))__atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);
    }
  }
done:
  pthread_mutex_lock(&relay_mutex);relay_done=1;
  pthread_cond_broadcast(&relay_condition);pthread_mutex_unlock(&relay_mutex);return NULL;
}
static int install_interrupts(qaplet_runtime *runtime){
  pthread_condattr_t attr;
  if(pthread_condattr_init(&attr))return 0;
  int error=pthread_condattr_setclock(&attr,CLOCK_MONOTONIC);
  if(!error)error=pthread_cond_init(&relay_condition,&attr);
  pthread_condattr_destroy(&attr);if(error)return 0;
  __atomic_store_n(&relay_runtime,runtime,__ATOMIC_SEQ_CST);
  if(IsWindows()){
#if defined(__x86_64__)
    int64_t kernel=LoadLibraryA("kernel32.dll");if(!kernel)return 0;
    raw_set_event=(__typeof__(raw_set_event))GetProcAddress(kernel,"SetEvent");
    raw_set_ctrl=(__typeof__(raw_set_ctrl))GetProcAddress(kernel,"SetConsoleCtrlHandler");
    cancel_synchronous=(__typeof__(cancel_synchronous))GetProcAddress(kernel,"CancelSynchronousIo");
    interrupt_event=CreateEvent(NULL,1,0,NULL);relay_event=CreateEvent(NULL,0,0,NULL);
    if(!raw_set_event || !raw_set_ctrl || !cancel_synchronous || !interrupt_event || !relay_event)return 0;
    /* Register only after every asynchronous target is fully initialized. */
    if(!raw_set_ctrl(windows_interrupt,1))return 0;
#else
    return 0;
#endif
  }else{
    if(pipe(interrupt_pipe))return 0;
    if(fcntl(interrupt_pipe[1],F_SETFL,O_NONBLOCK)<0 ||
        fcntl(interrupt_pipe[0],F_SETFD,FD_CLOEXEC)<0 || fcntl(interrupt_pipe[1],F_SETFD,FD_CLOEXEC)<0)return 0;
    struct sigaction action={0};action.sa_handler=unix_interrupt;
    sigemptyset(&action.sa_mask);sigaddset(&action.sa_mask,SIGINT);sigaddset(&action.sa_mask,SIGTERM);
    if(sigaction(SIGINT,&action,NULL) || sigaction(SIGTERM,&action,NULL))return 0;
  }
  pthread_attr_t thread_attr;
  if(pthread_attr_init(&thread_attr))return 0;
  error=pthread_attr_setstacksize(&thread_attr,1048576);
  if(!error)error=pthread_create(&relay_thread,&thread_attr,interrupt_relay,NULL);
  pthread_attr_destroy(&thread_attr);if(error)return 0;
  relay_started=1;return 1;
}
static void stop_relay(void){
  if(!relay_started)return;
  __atomic_store_n(&relay_shutdown,1,__ATOMIC_SEQ_CST);
  if(IsWindows()){if(!SetEvent(relay_event))__atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);}
  else{unsigned char hint=0;ssize_t n;do{n=write(interrupt_pipe[1],&hint,1);}while(n<0 && errno==EINTR);
    if(n<0 && errno!=EAGAIN && errno!=EWOULDBLOCK)__atomic_store_n(&interrupt_error,1,__ATOMIC_SEQ_CST);}
}
static int retire_relay(void){
  if(!relay_started)return 1;
  uint64_t before=begin_native_wait(),deadline_ns;
  if(cleanup_clock_failed || UINT64_MAX-before<cleanup_remaining_ns)return 0;
  deadline_ns=before+cleanup_remaining_ns;
  struct timespec deadline={deadline_ns/1000000000,deadline_ns%1000000000};
  pthread_mutex_lock(&relay_mutex);
  int error=0,joined=0;
  for(;;){
    if(relay_done){
      if(IsWindows()){
        /* The function's notification precedes pthread/TLS retirement. Wait
         * on the real thread, within the same monotonic cleanup deadline. */
        int64_t native=__atomic_load_n(&relay_native_handle,__ATOMIC_SEQ_CST);
        uint64_t current=0;
        if(!native)break;
        if(!clock_ns(&current) || current<before){cleanup_clock_failed=1;break;}
        uint32_t timeout=current<deadline_ns ? (deadline_ns-current)/1000000 : 0;
        pthread_mutex_unlock(&relay_mutex);
        uint32_t waited=WaitForSingleObject(native,timeout);
        error=waited==0 ? pthread_tryjoin_np(relay_thread,NULL) : ETIMEDOUT;
        pthread_mutex_lock(&relay_mutex);joined=!error;break;
      }
      error=pthread_tryjoin_np(relay_thread,NULL);if(!error){joined=1;break;}if(error!=EBUSY)break;
    }
    /* A done notification precedes actual pthread/TLS retirement. Never hide
     * an unbounded join behind it. A rare EBUSY waits the remaining deadline
     * without polling, then rechecks physical retirement once. */
    if(!cleanup_remaining_ns)break;
    error=pthread_cond_timedwait(&relay_condition,&relay_mutex,&deadline);
    if(error){if(error==ETIMEDOUT && relay_done)joined=!pthread_tryjoin_np(relay_thread,NULL);break;}
  }
  pthread_mutex_unlock(&relay_mutex);end_native_wait(before);
  if(joined){
    relay_started=0;__atomic_store_n(&relay_runtime,NULL,__ATOMIC_SEQ_CST);
    if(IsWindows()){
      int64_t native=__atomic_load_n(&relay_native_handle,__ATOMIC_SEQ_CST);
      if(!CloseHandle(native))return 0;
      __atomic_store_n(&relay_native_handle,0,__ATOMIC_SEQ_CST);
    }
  }return joined;
}

typedef struct host_output_record {
  struct NtOverlapped operation;
  struct host_output_record *next;
  int64_t handle,ready,thread_handle;
  pthread_t thread;
  uint32_t size,written,error;
  char data[65536];
} host_output_record;
static host_output_record *retained_output;
static int host_output_incomplete;
static int interrupt_reported;

static uint32_t output_wait(int64_t event){
  if(!interrupt_event)return WaitForSingleObject(event,0xffffffff);
  int64_t events[]={event,interrupt_event,relay_event};
  for(;;){
    if(__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST))return 1;
    uint32_t result=WaitForMultipleObjects(3,events,0,0xffffffff);
    if(result!=1 && result!=2)return result;
    /* Either independent native hint can wake final output. The auto-reset
     * relay event can also contain its earlier ordinary shutdown hint. */
  }
}
static uint32_t output_cleanup_wait(int64_t event){
  uint64_t before=begin_native_wait();uint32_t result=WaitForSingleObject(event,(uint32_t)remaining_ms());
  end_native_wait(before);return result;
}
static void retain_output(host_output_record *owned){
  owned->next=retained_output;retained_output=owned;host_output_incomplete=1;
}
static void *synchronous_output(void *context){
  host_output_record *owned=context;int64_t native=0;
  if(!DuplicateHandle(-1,-2,-1,&native,0,0,2))owned->error=GetLastError();
  __atomic_store_n(&owned->thread_handle,native,__ATOMIC_SEQ_CST);
  SetEvent(owned->ready);
  if(!owned->error && !WriteFile(owned->handle,owned->data,owned->size,&owned->written,NULL))owned->error=GetLastError();
  SetEvent(owned->operation.hEvent);return NULL;
}
static qaplet_code write_output(unsigned,const char *,size_t);
static void report_error(const char *);

static int finish(int status) {
  /* The supplied Cosmopolitan exit path encodes POSIX wait status on PE.
   * CLI status is a raw OS contract. Complete SDK cleanup and stdio first. */
  stop_relay();
  if(!retire_relay()){report_error("qaplet: native cleanup incomplete (interrupt relay retained)\n");status=1;}
  if(__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST)){
    if(!interrupt_reported)report_error("qaplet: EXECUTION_STOPPED (OS interrupt)\n");status=1;
  }
  if(host_output_incomplete){
    report_error("qaplet: Host output native cleanup incomplete; storage retained until process exit\n");
    if(!status)status=1;
  }
  fflush(NULL);
  if (IsWindows()) ExitProcess(status);
  return status;
}

static int invalid_arguments(void) {
  report_error("qaplet: INVALID_ARGUMENT\n");
  return finish(2);
}

static int parse_budget(const char *text, uint64_t *output) {
  if (!*text) return 0;
  uint64_t value=0;
  for (;*text;++text) {
    if (*text<'0' || *text>'9' || value>(UINT64_MAX-(unsigned)(*text-'0'))/10) return 0;
    value=value*10+(unsigned)(*text-'0');
  }
  *output=value;
  return 1;
}

typedef struct {
  int changed[3], flags[3];
  qaplet_io_endpoint pipes[3];
} cli_stdio;

/* The CLI is the Host of these inherited routes. It authorizes temporary
 * O_NONBLOCK on POSIX pipes, including the shared open file description; no
 * other participant may change flags or perform concurrent IO during this
 * ownership interval. The SDK itself continues to be a non-mutating borrower.
 * Unsupported/invalid routes remain absent from the first JS stdio snapshot.
 * In particular a synchronous Windows pipe is never treated as overlapped. */
static qaplet_code connect_stdio(qaplet_runtime *runtime,cli_stdio *host,qaplet_diagnostic *diagnostic) {
  qaplet_io_endpoint terminals[3];unsigned count=0;
  for(uint32_t stream=0;stream<3;++stream){
    qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;
    qaplet_io_endpoint endpoint=QAPLET_IO_ENDPOINT_INIT;
    qaplet_code code=qaplet_stdio_inspect(runtime,stream,&info,diagnostic);
    if(code)return code;
    if(info.kind==QAPLET_STDIO_TTY || info.kind==QAPLET_STDIO_CONSOLE){
      qaplet_tty_options options=QAPLET_TTY_OPTIONS_INIT;
      qaplet_tty_status status=QAPLET_TTY_STATUS_INIT;
      /* Join only the actual selected route. An unrelated terminal gets its
       * own explicit lease; no alternate device is opened. Zero flags preserve
       * terminal modes. JS may release this group, await closed, then acquire
       * raw/VT facades; exports are deliberately the first-import snapshot. */
      code=QAPLET_UNSUPPORTED;
      for(unsigned i=0;i<count && code==QAPLET_UNSUPPORTED;++i)
        code=qaplet_tty_join(runtime,&terminals[i],stream,&options,&endpoint,&status,diagnostic);
      if(code==QAPLET_UNSUPPORTED){
        code=qaplet_tty_acquire(runtime,stream,&options,&endpoint,&status,diagnostic);
        if(!code)terminals[count++]=endpoint;
      }
      if(code==QAPLET_UNSUPPORTED)continue;
      if(code)return code;
    }else{
      if(!IsWindows() && info.kind==QAPLET_STDIO_PIPE && !info.supported){
        int flags=fcntl(stream,F_GETFL);
        if(flags<0)return QAPLET_IO_ERROR;
        host->flags[stream]=flags;
        if(fcntl(stream,F_SETFL,flags|O_NONBLOCK)<0)return QAPLET_IO_ERROR;
        host->changed[stream]=1;info.supported=1;
      }
      if(!info.supported)continue;
      code=qaplet_stdio_attach(runtime,stream,&endpoint,&info,diagnostic);
      if(code)return code;
      host->pipes[stream]=endpoint;
    }
  }
  return QAPLET_OK;
}

static qaplet_code restore_stdio(qaplet_runtime *runtime,cli_stdio *host,int quiescent) {
  qaplet_code result=QAPLET_OK;
  for(int stream=2;stream>=0;--stream)if(host->changed[stream]){
    int retired=quiescent || !host->pipes[stream].generation;
    if(!retired){
      qaplet_io_endpoint_status status=QAPLET_IO_ENDPOINT_STATUS_INIT;
      qaplet_code code=qaplet_get_io_endpoint_status(runtime,&host->pipes[stream],&status,NULL);
      retired=code==QAPLET_INVALID_HANDLE || (!code && status.ended && !status.physical_pending);
    }
    /* An unrelated retained file/helper must not prevent restoring an already
     * retired pipe. Conversely a live operation keeps its flags until exit. */
    if(!retired || fcntl(stream,F_SETFL,host->flags[stream])<0)result=QAPLET_IO_ERROR;
    else host->changed[stream]=0;
  }
  return result;
}

static qaplet_code render_result(const qaplet_result *result,char **out,size_t *length) {
  size_t capacity=result->type==QAPLET_VALUE_BYTES ? result->length*2+3 :
                  result->type==QAPLET_VALUE_STRING ? result->length+1 : 128;
  char *text=malloc(capacity);if(!text)return QAPLET_MEMORY_LIMIT;size_t n=0;
  switch (result->type) {
    case QAPLET_VALUE_UNDEFINED: n=sprintf(text,"undefined");break;
    case QAPLET_VALUE_NULL: n=sprintf(text,"null");break;
    case QAPLET_VALUE_BOOL: n=sprintf(text,"%s",result->boolean ? "true" : "false");break;
    case QAPLET_VALUE_DOUBLE:
      if (isnan(result->number)) n=sprintf(text,"NaN");
      else if (isinf(result->number)) n=sprintf(text,"%s",signbit(result->number) ? "-Infinity" : "Infinity");
      else n=sprintf(text,"%.17g",result->number);
      break;
    case QAPLET_VALUE_INT64: n=sprintf(text,"%" PRId64,result->signed_integer);break;
    case QAPLET_VALUE_UINT64: n=sprintf(text,"%" PRIu64,result->unsigned_integer);break;
    case QAPLET_VALUE_STRING:
      n=result->length;if(n)memcpy(text,result->buffer,n);
      break;
    case QAPLET_VALUE_BYTES:
      text[n++]='0';text[n++]='x';
      for(size_t i=0;i<result->length;++i){unsigned byte=((unsigned char *)result->buffer)[i];text[n++]="0123456789abcdef"[byte>>4];text[n++]="0123456789abcdef"[byte&15];}
      break;
  }
  text[n++]='\n';*out=text;*length=n;return QAPLET_OK;
}

/* A one-shot Host write after native retirement, not another loop/SDK backend.
 * Win32 CRT offsets can be cached separately from the shared HANDLE position
 * advanced by managed IO. Use the actual selected route for final output too.
 * Console gets explicit UTF-16, independent of the console output code page;
 * its UTF-8 text boundary is the same strict scalar subset as the SDK adapter. */
static qaplet_code write_output(unsigned stream,const char *data,size_t size) {
  /* Early CLI routes (help/version/argument errors) have no Runtime. Retire
   * their ordinary relay before the owner consumes final-output wake hints. */
  if(relay_started && !__atomic_load_n(&relay_runtime,__ATOMIC_SEQ_CST)){
    stop_relay();if(!retire_relay())host_output_incomplete=1;
  }
  if(IsWindows()){
    int64_t handle=GetStdHandle(-10-(int)stream);uint32_t mode=0;
    if(!handle || handle==-1)return QAPLET_IO_ERROR;
    int console=GetConsoleMode(handle,&mode);
    int asynchronous=0;uint32_t type=0;
    if(!console){
      type=GetFileType(handle);if(!type)return QAPLET_IO_ERROR;
      if(type==1 || type==3){
        struct NtIoStatusBlock status={0};struct NtFileModeInformation file_mode={0};
        if(NtQueryInformationFile(handle,&status,&file_mode,sizeof(file_mode),kNtFileModeInformation)<0)return QAPLET_IO_ERROR;
        asynchronous=!(file_mode.Mode&0x30);
        if(type==1 && asynchronous)return QAPLET_UNSUPPORTED; /* no async-file expansion */
        if(type==3 && asynchronous){uint32_t flags=0,quota=0;
          if(!GetNamedPipeInfo(handle,&flags,&quota,NULL,NULL) || (flags&4) || !quota)return QAPLET_UNSUPPORTED;
        }
      }
    }
    while(size){
      uint32_t written=0;
      if(!console){
        uint32_t chunk=size>65536 ? 65536 : size;
        if(asynchronous){
          /* One bounded Host-owned operation after SDK retirement. Both the
           * unique OVERLAPPED/event and its copied bytes survive a pending IRP.
           * A failed completion query is not proof of physical retirement. */
          host_output_record *owned=malloc(sizeof(*owned));if(!owned)return QAPLET_MEMORY_LIMIT;
          owned->operation=(struct NtOverlapped){0};owned->next=NULL;memcpy(owned->data,data,chunk);
          owned->operation.hEvent=CreateEvent(NULL,1,0,NULL);
          if(!owned->operation.hEvent){free(owned);return QAPLET_IO_ERROR;}
          int accepted=WriteFile(handle,owned->data,chunk,NULL,&owned->operation);
          uint32_t error=accepted ? 0 : GetLastError();
          int stopped=0,terminal=1;
          if(accepted || error==997){
            uint32_t waited=output_wait(owned->operation.hEvent);
            if(waited==1){stopped=1;CancelIoEx(handle,&owned->operation);waited=output_cleanup_wait(owned->operation.hEvent);}
            if(waited!=0)terminal=0;
            else if(!GetOverlappedResult(handle,&owned->operation,&written,0) && GetLastError()!=995)terminal=0;
          }
          if(!terminal){
            retain_output(owned);return QAPLET_IO_ERROR;
          }
          if(!CloseHandle(owned->operation.hEvent)){
            owned->next=retained_output;retained_output=owned;host_output_incomplete=1;return QAPLET_IO_ERROR;
          }
          free(owned);
          if(stopped)return QAPLET_EXECUTION_STOPPED;
          if(!accepted && error!=997)return QAPLET_IO_ERROR;
        }else if(stream==2 && type==1){
          /* A bounded diagnostic to an actual synchronous regular file has no
           * retained IRP/Host thread. Do not require another helper retirement
           * after the shared cleanup wait budget is already exhausted. Like
           * any native syscall this is not a whole-function hard deadline. */
          if(!WriteFile(handle,data,chunk,&written,NULL))return QAPLET_IO_ERROR;
        }else if(interrupt_event){
          /* Synchronous inherited pipes are still unsupported by the SDK.
           * Final Host output runs on an owned helper: CancelSynchronousIo can
           * race the syscall's acceptance, so only actual thread retirement
           * permits freeing its copied bytes/stack; timeout retains all of it. */
          host_output_record *owned=calloc(1,sizeof(*owned));if(!owned)return QAPLET_MEMORY_LIMIT;
          owned->handle=handle;owned->size=chunk;memcpy(owned->data,data,chunk);
          owned->ready=CreateEvent(NULL,1,0,NULL);owned->operation.hEvent=CreateEvent(NULL,1,0,NULL);
          if(!owned->ready || !owned->operation.hEvent){retain_output(owned);return QAPLET_IO_ERROR;}
          pthread_attr_t attr;int error=pthread_attr_init(&attr);
          if(!error){error=pthread_attr_setstacksize(&attr,1048576);if(!error)error=pthread_create(&owned->thread,&attr,synchronous_output,owned);pthread_attr_destroy(&attr);}
          if(error){CloseHandle(owned->ready);CloseHandle(owned->operation.hEvent);free(owned);return QAPLET_IO_ERROR;}
          int stopped=output_wait(owned->ready)==1;
          if(stopped && output_cleanup_wait(owned->ready)!=0){retain_output(owned);return QAPLET_IO_ERROR;}
          int64_t thread_handle=__atomic_load_n(&owned->thread_handle,__ATOMIC_SEQ_CST);
          uint32_t waited=stopped ? 1 : output_wait(owned->operation.hEvent);
          if(waited==1){
            stopped=1;
#if defined(__x86_64__)
            if(thread_handle)cancel_synchronous(thread_handle);
#endif
            waited=output_cleanup_wait(owned->operation.hEvent);
          }
          if(waited!=0 || !thread_handle || output_cleanup_wait(thread_handle)!=0 || pthread_tryjoin_np(owned->thread,NULL)){
            retain_output(owned);return QAPLET_IO_ERROR;
          }
          written=owned->written;uint32_t native_error=owned->error;
          if(!CloseHandle(thread_handle) || !CloseHandle(owned->ready) || !CloseHandle(owned->operation.hEvent)){retain_output(owned);return QAPLET_IO_ERROR;}
          free(owned);if(stopped)return QAPLET_EXECUTION_STOPPED;if(native_error)return QAPLET_IO_ERROR;
        }else if(!WriteFile(handle,data,chunk,&written,NULL))return QAPLET_IO_ERROR;
        if(!written || written>chunk)return QAPLET_IO_ERROR;
        data+=written;size-=written;
      }else{
        char16_t wide[1024];uint32_t count=0;size_t used=0;
        while(used<size && count<1022){
          unsigned first=(unsigned char)data[used++],scalar=first,following=0,minimum=0;
          if(first>=128){if(first>=194 && first<=223){scalar=first&31;following=1;minimum=128;}
            else if(first<=239 && first>=224){scalar=first&15;following=2;minimum=2048;}
            else if(first<=244 && first>=240){scalar=first&7;following=3;minimum=65536;}
            else return QAPLET_INVALID_UTF8;}
          if(following>size-used)return QAPLET_INVALID_UTF8;
          for(unsigned i=0;i<following;++i){unsigned next=(unsigned char)data[used++];if((next&192)!=128)return QAPLET_INVALID_UTF8;scalar=(scalar<<6)|(next&63);}
          if(scalar<minimum || scalar>0x10ffff || (scalar>=0xd800 && scalar<=0xdfff))return QAPLET_INVALID_UTF8;
          if(scalar>65535){scalar-=65536;wide[count++]=0xd800+(scalar>>10);wide[count++]=0xdc00+(scalar&1023);}else wide[count++]=scalar;
        }
        uint32_t at=0;
        while(at<count){if(!WriteConsole(handle,wide+at,count-at,&written,NULL) || !written || written>count-at)return QAPLET_IO_ERROR;at+=written;}
        data+=used;size-=used;
      }
    }
    return QAPLET_OK;
  }
  /* The SDK and CLI preserve the same Host signal and descriptor state. */
  int failed=0,flags=fcntl(stream,F_GETFL),changed=0;
  if(flags<0)failed=1;
  else if(!(flags&O_NONBLOCK)){if(fcntl(stream,F_SETFL,flags|O_NONBLOCK)<0)failed=1;else changed=1;}
  while(size && !failed){
    if(stream==1 && __atomic_load_n(&interrupted,__ATOMIC_SEQ_CST)){failed=1;break;}
    ssize_t n=qaplet_pipe_write(stream,data,size);
    if(n<0 && errno==EINTR)continue;
    if(n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)){
      if(__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST)){failed=1;break;}
      struct pollfd routes[]={{stream,POLLOUT,0},{interrupt_pipe[0],POLLIN,0}};
      int ready;do{ready=poll(routes,interrupt_pipe[0]>=0 ? 2 : 1,-1);}while(ready<0 && errno==EINTR && !__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST));
      if(ready<=0){failed=1;break;}
      /* After relay retirement its explicit shutdown hint may remain. */
      if(routes[1].revents && !__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST)){unsigned char hints[128];read(interrupt_pipe[0],hints,sizeof(hints));}
      continue;
    }
    if(n<=0){failed=1;break;}data+=n;size-=n;
  }
  if(changed && fcntl(stream,F_SETFL,flags)<0)failed=1;
  return failed ? QAPLET_IO_ERROR : QAPLET_OK;
}

static qaplet_code output_result(const qaplet_result *result) {
  char *data=NULL;size_t size=0;qaplet_code code=render_result(result,&data,&size);
  if(!code)code=write_output(1,data,size);free(data);return code;
}
static void report_error(const char *message) {write_output(2,message,strlen(message));}

int main(int argc, char **argv) {
  if(!install_interrupts(NULL)){report_error("qaplet: interrupt setup failed\n");return finish(1);}
  qaplet_config config = QAPLET_CONFIG_INIT;
  int position=1, seen_timeout=0, seen_dispatches=0;
  while (position<argc && (!strcmp(argv[position],"--timeout-ms") || !strcmp(argv[position],"--max-dispatches"))) {
    int timeout=!strcmp(argv[position],"--timeout-ms");
    uint64_t value;
    if (position+1>=argc || !parse_budget(argv[position+1],&value) ||
        (timeout && (seen_timeout++ || value>UINT64_MAX/1000000)) || (!timeout && seen_dispatches++))
      return invalid_arguments();
    if (timeout) config.limits.execution_timeout_ms=value;
    else config.limits.execution_dispatches=value;
    position+=2;
  }
  if (argc-position==1 && !strcmp(argv[position],"--help")) {
    const char *text="Usage: qaplet [--timeout-ms N] [--max-dispatches N] run <file> [-- args...]\n"
          "       qaplet [--timeout-ms N] [--max-dispatches N] eval --source <text>\n"
          "       qaplet --help | --version\n"
          "run uses ESM; argv contains only args after --. eval uses Script.\n"
          "eval prints scalar values, raw UTF-8 strings, or 0x-prefixed hex bytes, then a newline.\n"
          "Zero budgets use finite SDK defaults. Exit: 0 success, 1 execution, 2 arguments.\n";
    return finish(write_output(1,text,strlen(text)) ? 1 : 0);
  }
  if (argc-position==1 && !strcmp(argv[position],"--version")) {
    char text[64];snprintf(text,sizeof(text),"qaplet 0.1.0 (Embed SDK ABI %u)\n",QAPLET_ABI_VERSION);
    return finish(write_output(1,text,strlen(text)) ? 1 : 0);
  }
  int is_run=position<argc && !strcmp(argv[position],"run");
  const char *input=NULL;
  qaplet_string *arguments=NULL;
  if (is_run) {
    if (argc-position<2 || (argc-position>2 && strcmp(argv[position+2],"--"))) return invalid_arguments();
    input=argv[position+1];
    if (!*input) return invalid_arguments();
    if (argc-position>3) {
      config.argc=(size_t)(argc-position-3);
      arguments=calloc(config.argc,sizeof(*arguments));
      if (!arguments) { report_error("qaplet: code=11 MEMORY_LIMIT\n"); return finish(1); }
      for (size_t i=0;i<config.argc;++i) arguments[i]=(qaplet_string){argv[position+3+i],strlen(argv[position+3+i])};
      config.argv=arguments;
    }
  } else {
    if (argc-position!=3 || strcmp(argv[position],"eval") || strcmp(argv[position+1],"--source")) return invalid_arguments();
    input=argv[position+2];
  }
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_diagnostic diagnostic = QAPLET_DIAGNOSTIC_INIT;
  qaplet_m2_config m2 = QAPLET_M2_CONFIG_INIT;
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  cli_stdio host={0};
  char message[1024]; diagnostic.message=message; diagnostic.capacity=sizeof(message);
  qaplet_code code=qaplet_create_m2(&config,&m2,&runtime,&diagnostic);
  free(arguments);
  if(!code){
    __atomic_store_n(&relay_runtime,runtime,__ATOMIC_SEQ_CST);
    if(__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST)){qaplet_request_stop(runtime,NULL);code=QAPLET_EXECUTION_STOPPED;strcpy(message,"EXECUTION_STOPPED");}
  }
  if(!code)code=connect_stdio(runtime,&host,&diagnostic);
  if (!code) code=is_run ? qaplet_run_entry(runtime,input,strlen(input),&progress,&diagnostic) :
                          qaplet_eval(runtime,input,strlen(input),&progress,&diagnostic);
  int unsettled=0;
  while (!code && progress.phase==QAPLET_PHASE_PENDING) {
    code=qaplet_drive_entry(runtime,&drive,&diagnostic);
    progress.phase=drive.phase;
    if (!code && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY) { unsettled=1; break; }
  }
  if (!code && !unsettled && !is_run) code=qaplet_get_result(runtime,&result,&diagnostic);
  if (code==QAPLET_BUFFER_TOO_SMALL) {
    result.buffer=malloc(result.length); result.capacity=result.length;
    if (!result.buffer) { code=QAPLET_MEMORY_LIMIT; strcpy(message,"MEMORY_LIMIT"); }
    else code=qaplet_get_result(runtime,&result,&diagnostic);
  }
  /* Retain the copied entry result before later independent deliveries can
   * replace the SDK's most recent result. */
  if (!code && !unsettled) {
    code=qaplet_run(runtime,&drive,&diagnostic);
    if (!code && drive.reason==QAPLET_DRIVE_UNSETTLED_ENTRY) unsettled=1;
  }
  qaplet_code execution_code=code,cleanup=QAPLET_OK,restoration=QAPLET_OK;
  stop_relay();
  if (runtime) {
    cleanup=qaplet_close(runtime,NULL);
    if (!cleanup){uint64_t before=begin_native_wait();cleanup=qaplet_quiesce(runtime,remaining_ms(),&drive,NULL);end_native_wait(before);}
    if (!cleanup && drive.reason!=QAPLET_DRIVE_QUIESCENT) cleanup=QAPLET_BUSY;
    restoration=restore_stdio(runtime,&host,!cleanup);
    if(!retire_relay() && !cleanup)cleanup=QAPLET_BUSY;
    if(cleanup_clock_failed && !cleanup)cleanup=QAPLET_CONTROL_FAILURE;
    if (!cleanup) cleanup=qaplet_destroy(runtime,NULL);
  }
  /* SDK byte writes have retired before the Host's final result/diagnostics
   * use these same routes. Blocking flags and complete terminal modes have
   * been restored; a large eval result must not silently truncate on EAGAIN. */
  if (!code && !unsettled && !cleanup && !restoration && !is_run){
    code=output_result(&result);
    if(code){char text[128];snprintf(text,sizeof(text),"qaplet: code=%u %s (entry result output)\n",code,code==QAPLET_EXECUTION_STOPPED ? "EXECUTION_STOPPED" : code==QAPLET_MEMORY_LIMIT ? "MEMORY_LIMIT" : code==QAPLET_INVALID_UTF8 ? "INVALID_UTF8" : code==QAPLET_UNSUPPORTED ? "UNSUPPORTED" : "IO_ERROR");report_error(text);}
  }
  free(result.buffer);
  if (execution_code){char text[1200];snprintf(text,sizeof(text),"qaplet: code=%u %s\n",(unsigned)execution_code,message);report_error(text);if(execution_code==QAPLET_EXECUTION_STOPPED)interrupt_reported=1;}
  if (unsettled) report_error("qaplet: UNSETTLED_ENTRY\n");
  if(cleanup){char text[128];snprintf(text,sizeof(text),"qaplet: native cleanup incomplete code=%u\n",(unsigned)cleanup);report_error(text);}
  if(restoration){char text[128];snprintf(text,sizeof(text),"qaplet: standard stream restoration incomplete code=%u\n",(unsigned)restoration);report_error(text);}
  if(__atomic_load_n(&interrupted,__ATOMIC_SEQ_CST) && execution_code!=QAPLET_EXECUTION_STOPPED){report_error("qaplet: EXECUTION_STOPPED (OS interrupt)\n");interrupt_reported=1;code=QAPLET_EXECUTION_STOPPED;}
  if(__atomic_load_n(&interrupt_error,__ATOMIC_SEQ_CST)){report_error("qaplet: interrupt control failure; native storage retained until process exit\n");code=QAPLET_CONTROL_FAILURE;}
  if(cleanup || restoration)code=cleanup ? cleanup : restoration;
  return finish(code || unsettled ? 1 : 0);
}
