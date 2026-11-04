/* SPDX-License-Identifier: MIT */
/* Public SDK + real OS boundary; never inspect private Runtime/engine data. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"
#include "libc/str/str.h"
#include "libc/nt/struct/byhandlefileinformation.h"
extern int64_t CreateFile(const char16_t *,uint32_t,uint32_t,void *,uint32_t,uint32_t,int64_t);
extern bool32 GetFileInformationByHandle(int64_t,struct NtByHandleFileInformation *);
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"process contract line %d: %s errno=%d\n",__LINE__,#c,errno); return 1; } } while (0)
static const char *self,*launcher;
static char executable[PATH_MAX],root[PATH_MAX],cwd[PATH_MAX],native_cwd[PATH_MAX];
static const char *special[]={"","参数 空格","two words","quote\"double","slash\\tail\\","literal&|;$()<>*","apostrophe'"};
static qaplet_string arguments[16];
static double milliseconds(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000.0+ts.tv_nsec/1000000.0; }
static qaplet_code spawn_ok(qaplet_runtime *r,const qaplet_process_options *options,qaplet_process *p,qaplet_process_status *status,qaplet_diagnostic *diagnostic) {
  qaplet_code code=qaplet_process_spawn(r,options,p,status,diagnostic);
  if (code!=QAPLET_OK) {
    int saved_errno=errno;struct rlimit limits={0};int result=getrlimit(RLIMIT_NOFILE,&limits),limit_error=result ? errno : 0;
    fprintf(stderr,"process spawn failure: return_code=%d status_code=%d error_domain=%u native_error=%d helper_active=%u nofile_result=%d nofile_error=%d nofile_soft=%llu nofile_hard=%llu int_max=%d\n",
      code,status->code,status->error_domain,status->error,status->helper_active,result,limit_error,
      (unsigned long long)limits.rlim_cur,(unsigned long long)limits.rlim_max,INT_MAX);
    errno=saved_errno;
  }
  return code;
}
static int copy_file(const char *from,const char *to) {
  int in=open(from,O_RDONLY|O_CLOEXEC),out=open(to,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0700);
  CHECK(in>=0 && out>=0); char data[16384]; ssize_t n;
  while ((n=read(in,data,sizeof(data)))>0) CHECK(write(out,data,n)==n);
  CHECK(n==0 && !close(in) && !close(out)); return 0;
}
static void prepare(qaplet_process_options *options,const char *mode) {
  *options=(qaplet_process_options)QAPLET_PROCESS_OPTIONS_INIT;
  options->path=(qaplet_string){executable,strlen(executable)};
  for (int i=0;i<3;++i) options->stdio[i]=QAPLET_PROCESS_IGNORE;
  size_t n=0;
  arguments[n++]=(qaplet_string){executable,strlen(executable)};
  if (!IsWindows()) { arguments[n++]=(qaplet_string){self,strlen(self)}; arguments[n++]=(qaplet_string){"--m2-process-child",18}; }
  arguments[n++]=(qaplet_string){mode,strlen(mode)};
  for (size_t i=0;i<7;++i) arguments[n++]=(qaplet_string){special[i],strlen(special[i])};
  options->argv=arguments; options->argc=n;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(runtime,5000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  qaplet_process_snapshot p=QAPLET_PROCESS_SNAPSHOT_INIT; qaplet_m2_snapshot m=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(runtime,&p,NULL)==QAPLET_OK && !p.processes && !p.helper_threads && !p.helper_stack_bytes && !p.environment_bytes);
  CHECK(qaplet_get_m2_snapshot(runtime,&m,NULL)==QAPLET_OK && !m.native_handles && !m.reactor_registrations && !m.native_uncertain_handles && !m.engine_current_bytes);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK); return 0;
}
static int completed(qaplet_runtime *r,qaplet_process *p,uint32_t expected) {
  qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  CHECK(qaplet_run(r,&d,NULL)==QAPLET_OK && d.reason==QAPLET_DRIVE_IDLE && d.progressed);
  CHECK(qaplet_process_get_status(r,p,&s,NULL)==QAPLET_OK && s.exited && s.reaped && s.delivered && !s.referenced && !s.code && s.native_exit_code==expected && !s.native_signal);
  CHECK(qaplet_process_release(r,p,NULL)==QAPLET_OK);
  CHECK(qaplet_process_release(r,p,NULL)==QAPLET_INVALID_HANDLE);
  return 0;
}
static int inputs(void) {
  CHECK(!setenv("QAPLET_SNAPSHOT","frozen",1));
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  b.cwd=cwd; b.cwd_length=strlen(cwd);
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  CHECK(!setenv("QAPLET_SNAPSHOT","changed",1)); CHECK(!setenv("QAPLET_AFTER_CREATE","leak",1));
  qaplet_process_options o; prepare(&o,"--snapshot");
  o.path.data=strrchr(executable,'/')+1; o.path.length=strlen(o.path.data);
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK); CHECK(!completed(r,&p,37));
  prepare(&o,"--check");
  char envcwd[PATH_MAX+32]; snprintf(envcwd,sizeof(envcwd),"QAPLET_CWD=%s",native_cwd);
  qaplet_string env[]={ {"QAPLET_ONLY=完整 值",strlen("QAPLET_ONLY=完整 值")},{envcwd,strlen(envcwd)} };
  o.flags=QAPLET_PROCESS_REPLACE_ENV; o.env=env; o.env_count=2;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK);
  memset(envcwd,'?',strlen(envcwd)); arguments[0]=(qaplet_string){"modified",8};
  CHECK(!completed(r,&p,37));
  prepare(&o,"--empty"); o.flags=QAPLET_PROCESS_REPLACE_ENV;
  CHECK(spawn_ok(r,&o,&p,&s,NULL)==QAPLET_OK); CHECK(!completed(r,&p,37));
  CHECK(!finish(r)); unsetenv("QAPLET_AFTER_CREATE"); unsetenv("QAPLET_SNAPSHOT");
  puts("M2 process inputs: explicit Unicode path; copied argv/env/cwd; factory snapshot and complete replacement; actual exit/reap: pass"); return 0;
}
static int inherit(void) {
  char name[PATH_MAX],identity[128]="197";
  snprintf(name,sizeof(name),"%s/sentinel",cwd);
  int sentinel=-1; int64_t native=0;
  if (IsWindows()) {
    char16_t path[PATH_MAX]; tprecode8to16(path,PATH_MAX,native_cwd);
    size_t n=0; while(path[n]) ++n; const char16_t suffix[]=u"\\sentinel"; memcpy(path+n,suffix,sizeof(suffix));
    struct { uint32_t size; void *descriptor; int32_t inherited; } security={sizeof(security),NULL,1};
    native=CreateFile(path,0xc0000000,7,&security,2,128,0); CHECK(native && native!=-1);
    struct NtByHandleFileInformation info; CHECK(GetFileInformationByHandle(native,&info));
    snprintf(identity,sizeof(identity),"%lld:%u:%u:%u",(long long)native,info.dwVolumeSerialNumber,info.nFileIndexHigh,info.nFileIndexLow);
  } else { sentinel=open(name,O_CREAT|O_TRUNC|O_RDWR,0600); CHECK(sentinel>=0 && dup2(sentinel,197)==197 && !fcntl(197,F_SETFD,0)); }
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  int io[3][2],saved[3];
  for(int i=0;i<3;++i) { saved[i]=dup(i); CHECK(saved[i]>=0 && !pipe2(io[i],O_CLOEXEC)); CHECK(dup2(io[i][i ? 1 : 0],i)==i); }
  qaplet_process_options o; prepare(&o,"--inherit"); for (int i=0;i<3;++i) o.stdio[i]=QAPLET_PROCESS_INHERIT;
  arguments[IsWindows() ? 2 : 4]=(qaplet_string){identity,strlen(identity)};
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  qaplet_code code=spawn_ok(r,&o,&p,&s,NULL);
  for(int i=0;i<3;++i) { CHECK(dup2(saved[i],i)==i); close(saved[i]); close(io[i][i ? 1 : 0]); }
  CHECK(code==QAPLET_OK);
  const unsigned char input[]={'I',0,255,'N','\n'},errors[]={'E',0,254,'R'};
  CHECK(write(io[0][1],input,sizeof(input))==sizeof(input)); close(io[0][1]);
  unsigned char output[16]={0},error[16]={0};
  ssize_t output_bytes=read(io[1][0],output,sizeof(output)),error_bytes=read(io[2][0],error,sizeof(error));
  if (output_bytes!=sizeof(input) || error_bytes!=sizeof(errors)) {
    fprintf(stderr,"inherit child stdout=%ld stderr=%ld: ",(long)output_bytes,(long)error_bytes);
    if (error_bytes>0) fwrite(error,1,error_bytes,stderr);
    char diagnostic[256]; ssize_t n; while ((n=read(io[2][0],diagnostic,sizeof(diagnostic)))>0) fwrite(diagnostic,1,n,stderr);
    fputc('\n',stderr);
  }
  CHECK(output_bytes==sizeof(input) && !memcmp(output,input,sizeof(input)) && read(io[1][0],output,sizeof(output))==0);
  CHECK(error_bytes==sizeof(errors) && !memcmp(error,errors,sizeof(errors)) && read(io[2][0],error,sizeof(error))==0);
  close(io[1][0]); close(io[2][0]); CHECK(!completed(r,&p,37)); CHECK(!finish(r));
  if (IsWindows()) CHECK(CloseHandle(native)); else { CHECK(!close(197)); CHECK(!close(sentinel)); }
  CHECK(!unlink(name));
  puts("M2 process inherit: binary authorized stdio; extra native descriptor excluded; actual exit/reap: pass"); return 0;
}
static int errors(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; m.limits.process_arguments=16;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_process_options o; prepare(&o,"--empty");
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
  o.stdio[0]=QAPLET_PROCESS_PIPE; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_UNSUPPORTED && !p.runtime_id);
  prepare(&o,"--empty"); o.path=(qaplet_string){"bad\0path",8}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT);
  o.path=(qaplet_string){"\xff",1}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_UTF8);
  prepare(&o,"--empty"); o.argc=17; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_RESOURCE_LIMIT);
  prepare(&o,"--empty"); qaplet_string env[]={ {"NAME=a",6},{"NAME=b",6} }; o.flags=QAPLET_PROCESS_REPLACE_ENV; o.env=env; o.env_count=2;
  CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT);
  env[0]=(qaplet_string){"=bad",4}; o.env_count=1; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_INVALID_ARGUMENT);
  prepare(&o,"--empty"); o.path=(qaplet_string){"sh",2}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_PROCESS_ERROR && s.error>0 && s.error_domain==(IsWindows() ? QAPLET_ERROR_DOMAIN_WIN32 : QAPLET_ERROR_DOMAIN_ERRNO));
  prepare(&o,"--empty"); o.cwd=(qaplet_string){"missing-cwd",11}; CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_PROCESS_ERROR && s.error>0);
  if (IsWindows()) {
    char *long_argument=malloc(32768); CHECK(long_argument); memset(long_argument,'x',32768);
    prepare(&o,"--empty"); arguments[2]=(qaplet_string){long_argument,32768};
    CHECK(qaplet_process_spawn(r,&o,&p,&s,NULL)==QAPLET_VALUE_TOO_LARGE);
    free(long_argument);
  }
  qaplet_result result=QAPLET_RESULT_INIT; CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
  qaplet_snapshot snap=QAPLET_SNAPSHOT_INIT; CHECK(qaplet_get_snapshot(r,&snap,NULL)==QAPLET_OK && snap.lifecycle==QAPLET_OPEN);
  CHECK(!finish(r));
  /* A missing Host stdin must remain missing. An internal waker allocated
   * after this close cannot become an implicitly authorized child stream. */
  int saved=dup(0); CHECK(saved>=0 && !close(0));
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  prepare(&o,"--empty"); o.stdio[0]=QAPLET_PROCESS_INHERIT;
  qaplet_code invalid_stdio=qaplet_process_spawn(r,&o,&p,&s,NULL);
  CHECK(dup2(saved,0)==0 && !close(saved));
  CHECK(!finish(r)); CHECK(invalid_stdio==QAPLET_PROCESS_ERROR);
  puts("M2 process errors: strict UTF-8/NUL/limits; no PATH search; missing cwd/program native errors; result retained; safe rollback: pass"); return 0;
}
static int hanging(qaplet_runtime *r,qaplet_process *p) {
  int in[2],out[2],savein=dup(0),saveout=dup(1); CHECK(savein>=0 && saveout>=0 && !pipe2(in,O_CLOEXEC) && !pipe2(out,O_CLOEXEC));
  CHECK(dup2(in[0],0)==0 && dup2(out[1],1)==1);
  qaplet_process_options o; prepare(&o,"--hang"); o.stdio[0]=o.stdio[1]=QAPLET_PROCESS_INHERIT;
  qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT; qaplet_code code=spawn_ok(r,&o,p,&s,NULL);
  CHECK(dup2(savein,0)==0 && dup2(saveout,1)==1); close(savein);close(saveout);close(in[0]);close(out[1]);
  CHECK(code==QAPLET_OK); char byte=0; CHECK(read(out[0],&byte,1)==1 && byte=='R'); close(out[0]);
  /* Parent keeps input open until the process is reaped. This explicit gate
   * proves the child is running without random sleeps or private state. */
  return in[1];
}
static int cleanup(void) {
  for (int unref=0;unref<2;++unref) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT; m.limits.processes=1; m.limits.helper_threads=1; m.limits.native_handles=16; m.limits.reactor_registrations=2;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_process p=QAPLET_PROCESS_INIT; int gate=hanging(r,&p); CHECK(gate>=0);
    qaplet_process_snapshot ps=QAPLET_PROCESS_SNAPSHOT_INIT; CHECK(qaplet_get_process_snapshot(r,&ps,NULL)==QAPLET_OK && ps.processes==1 && ps.referenced==1 && ps.helper_threads==1 && ps.helper_stack_bytes==1024*1024);
    qaplet_process_options o; prepare(&o,"--empty"); qaplet_process extra=QAPLET_PROCESS_INIT; qaplet_process_status s=QAPLET_PROCESS_STATUS_INIT;
    CHECK(qaplet_process_spawn(r,&o,&extra,&s,NULL)==QAPLET_RESOURCE_LIMIT);
    CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_BUSY);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    if (unref) { CHECK(qaplet_process_set_ref(r,&p,0,NULL)==QAPLET_OK); CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE); }
    else {
      CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
      CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);
      qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_EXECUTION_STOPPED);
      double until=milliseconds()+5000;
      do { CHECK(qaplet_process_get_status(r,&p,&s,NULL)==QAPLET_OK); if (s.reaped) break; sched_yield(); } while (milliseconds()<until);
      CHECK(s.reaped && (IsWindows() ? s.native_exit_code==0xD00D : s.native_signal==9));
    }
    CHECK(!finish(r)); close(gate);
  }
  puts("M2 process cleanup: default ref; independent process/helper/handle limits; unref stays alive; stop force/reap; close/quiesce/destroy: pass"); return 0;
}
static int limits(void) {
  for (int kind=0;kind<4;++kind) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
    m.limits.processes=kind==0 ? 1 : 2; m.limits.helper_threads=kind==1 ? 1 : 2;
    m.limits.native_handles=kind==2 ? (IsWindows() ? 11 : 8) : 32;
    m.limits.reactor_registrations=kind==3 ? 2 : 4;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    qaplet_process p=QAPLET_PROCESS_INIT; int gate=hanging(r,&p); CHECK(gate>=0);
    qaplet_process_options o; prepare(&o,"--empty");
    qaplet_process extra=QAPLET_PROCESS_INIT; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
    CHECK(qaplet_process_spawn(r,&o,&extra,&status,NULL)==QAPLET_RESOURCE_LIMIT && !extra.runtime_id);
    qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;
    CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && snapshot.native_handles_peak<=m.limits.native_handles && snapshot.reactor_registrations<=m.limits.reactor_registrations);
    CHECK(!finish(r)); close(gate);
  }
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  b.limits.native_storage_bytes=1048576; m.limits.operation_data_bytes=262144; m.limits.process_data_bytes=1048576;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT; CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK);
  size_t length=snapshot.limits.native_storage_bytes-snapshot.native_current_bytes+1;
  CHECK(length<snapshot.m2_limits.process_data_bytes);
  char *large=malloc(1048577); CHECK(large); memset(large,'a',1048577);
  qaplet_process_options o; prepare(&o,"--empty"); qaplet_string argument={large,length}; o.argv=&argument; o.argc=1;
  qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  CHECK(qaplet_process_spawn(r,&o,&p,&status,NULL)==QAPLET_RESOURCE_LIMIT);
  argument.length=1048577;
  CHECK(qaplet_process_spawn(r,&o,&p,&status,NULL)==QAPLET_RESOURCE_LIMIT);
  CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && !snapshot.termination_reason);
  free(large); CHECK(!finish(r));
  char environment[16385];memset(environment,'v',sizeof(environment)-1);environment[sizeof(environment)-1]=0;
  CHECK(!setenv("QAPLET_PROCESS_BUDGET_ONLY",environment,1));
  extern char **environ;size_t environment_bytes=0;
  for(size_t i=0;environ[i];++i) environment_bytes+=strlen(environ[i])+1;
  b=(qaplet_config)QAPLET_CONFIG_INIT;m=(qaplet_m2_config)QAPLET_M2_CONFIG_INIT;m.limits.process_data_bytes=environment_bytes+1024;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);unsetenv("QAPLET_PROCESS_BUDGET_ONLY");
  char argument_bytes[1024];memset(argument_bytes,'a',sizeof(argument_bytes));argument=(qaplet_string){argument_bytes,sizeof(argument_bytes)};
  prepare(&o,"--empty");o.argv=&argument;o.argc=1;
  qaplet_code combined=qaplet_process_spawn(r,&o,&p,&status,NULL);
  CHECK(!finish(r));CHECK(combined==QAPLET_RESOURCE_LIMIT);
  puts("M2 process limits: independent process/helper/native handle/registration quotas; encoded bytes and simultaneous native total; ordinary refusal: pass"); return 0;
}
static int observed_exit(qaplet_runtime *r,qaplet_process *p,qaplet_process_status *s) {
  double until=milliseconds()+5000;
  do {
    CHECK(qaplet_process_get_status(r,p,s,NULL)==QAPLET_OK);
    if (s->reaped && !s->helper_active) return 0;
    sched_yield();
  } while (milliseconds()<until);
  CHECK(0); return 1;
}
static int ledger(void) {
  for (int pending=0;pending<2;++pending) {
    qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
    b.limits.execution_dispatches=1;
    qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    const char *source=pending ? "new Promise(()=>{})" : "42";
    qaplet_progress progress=QAPLET_PROGRESS_INIT;
    CHECK(qaplet_eval(r,source,strlen(source),&progress,NULL)==QAPLET_OK);
    qaplet_process_options o; prepare(&o,"--empty"); o.flags=QAPLET_PROCESS_REPLACE_ENV;
    qaplet_process p=QAPLET_PROCESS_INIT; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
    CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
    CHECK(status.reaped && !status.delivered && status.referenced);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.progressed);
    qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT; CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK);
    CHECK(snapshot.execution_dispatches==(pending ? 1 : 0));
    CHECK(qaplet_process_get_status(r,&p,&status,NULL)==QAPLET_OK && status.delivered && !status.referenced);
    CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
    if (pending) {
      CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
      CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_DISPATCH_LIMIT);
      CHECK(qaplet_get_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_FAILED && snapshot.termination_reason==QAPLET_DISPATCH_LIMIT && snapshot.execution_dispatches==1);
    } else {
      qaplet_result result=QAPLET_RESULT_INIT; CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
      CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK);
      CHECK(qaplet_process_set_ref(r,&p,0,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
      CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && !drive.progressed);
      CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
      CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK);
    }
    CHECK(!finish(r));
  }
  puts("M2 process ledger: native observation precedes owner handoff; pending entry cumulative dispatch; pure C preserves completed JS result: pass"); return 0;
}
static qaplet_code quick_work(qaplet_work_context *context,const void *input,size_t length,qaplet_work_output *output,void *userdata) {
  (void)context;(void)input;(void)length;(void)output;(void)userdata;return QAPLET_OK;
}
static void counted_timer(qaplet_runtime *r,const qaplet_timer *timer,void *userdata) { (void)r;(void)timer;++*(unsigned *)userdata; }
static int scheduling(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  b.limits.event_queue_slots=1; m.limits.processes=40;
  qaplet_runtime *r=NULL; CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_work_handler handler=QAPLET_WORK_HANDLER_INIT;
  handler.name="ready";handler.name_length=5;handler.function=quick_work;handler.input_bytes=handler.output_bytes=1;
  CHECK(qaplet_register_work_handler(r,&handler,NULL)==QAPLET_OK);
  const char source[]="Promise.all([import('qaplet:events'),import('qaplet:timers')]).then(([e,t])=>{e.onEvent(()=>{globalThis.processEvent=1});globalThis.installImmediate=()=>{t.setImmediate(()=>{globalThis.processImmediate=1})}})";
  qaplet_progress progress=QAPLET_PROGRESS_INIT; CHECK(qaplet_eval(r,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  qaplet_process processes[40]; qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  qaplet_process_options o; prepare(&o,"--empty");o.flags=QAPLET_PROCESS_REPLACE_ENV;
  for(unsigned i=0;i<40;++i) {
    processes[i]=(qaplet_process)QAPLET_PROCESS_INIT;
    CHECK(spawn_ok(r,&o,&processes[i],&status,NULL)==QAPLET_OK);
    CHECK(!observed_exit(r,&processes[i],&status) && !status.delivered);
  }
  qaplet_work_task tasks[16];
  for(unsigned i=0;i<16;++i) { tasks[i]=(qaplet_work_task)QAPLET_WORK_TASK_INIT;CHECK(qaplet_work_submit(r,"ready",5,NULL,0,&tasks[i],NULL)==QAPLET_OK); }
  qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;double until=milliseconds()+5000;
  do { CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK);if(work.completed==16)break;sched_yield(); } while(milliseconds()<until);
  CHECK(work.completed==16 && work.pending_handoffs==16);
  unsigned timers=0; qaplet_timer timer=QAPLET_TIMER_INIT;CHECK(qaplet_timer_create(r,0,counted_timer,&timers,&timer,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(r,"installImmediate()",18,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_post_event(r,1,0,NULL,0,NULL)==QAPLET_OK && qaplet_post_event(r,2,0,NULL,0,NULL)==QAPLET_QUEUE_FULL);
  CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
  qaplet_process_snapshot native=QAPLET_PROCESS_SNAPSHOT_INIT;qaplet_snapshot events=QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(r,&native,NULL)==QAPLET_OK && native.completed==14 && native.referenced==26);
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.pending_handoffs==1);
  CHECK(qaplet_get_snapshot(r,&events,NULL)==QAPLET_OK && events.events_consumed==1 && !events.queue_depth && timers==1);
  CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(qaplet_get_process_snapshot(r,&native,NULL)==QAPLET_OK && native.completed==40 && !native.referenced);
  CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && !work.pending_handoffs);
  for(unsigned i=0;i<40;++i) CHECK(qaplet_process_release(r,&processes[i],NULL)==QAPLET_OK);
  for(unsigned i=0;i<16;++i) CHECK(qaplet_work_release(r,&tasks[i],NULL)==QAPLET_OK);
  const char result_source[]="globalThis.processEvent===1&&globalThis.processImmediate===1";
  CHECK(qaplet_eval(r,result_source,sizeof(result_source)-1,&progress,NULL)==QAPLET_OK);
  qaplet_result result=QAPLET_RESULT_INIT;CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(!finish(r));
  puts("M2 process scheduling: 40 process and 16 work completions share 32 deliveries with timer/immediate/event; fair provider cursor; reliable saturated queue: pass");return 0;
}
static int liveness(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  qaplet_runtime *r=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  const char setup[]="import('qaplet:timers').then(t=>{globalThis.hits='';globalThis.install=()=>{t.setTimeout(()=>{hits+='T'},0).unref();t.setImmediate(()=>{hits+='I'}).unref()}})";
  CHECK(qaplet_eval(r,setup,sizeof(setup)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK);
  qaplet_process p=QAPLET_PROCESS_INIT;int gate=hanging(r,&p);CHECK(gate>=0);
  CHECK(qaplet_eval(r,"install()",9,&progress,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK && drive.progressed);
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_eval(r,"hits==='TI'",11,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(qaplet_process_set_ref(r,&p,0,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(r,"hits='';install()",17,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE && !drive.progressed);
  CHECK(qaplet_eval(r,"hits==='';",10,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(qaplet_pump(r,&progress,NULL)==QAPLET_OK && progress.progressed);
  CHECK(qaplet_eval(r,"hits==='TI'",11,&progress,NULL)==QAPLET_OK && qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.boolean);
  CHECK(!finish(r));close(gate);
  puts("M2 process liveness: referenced child permits ready unref timer/immediate; unref child returns naturally; explicit pump delivers; physical cleanup: pass");return 0;
}
static int identity(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;m.limits.processes=1;
  qaplet_runtime *r=NULL,*other=NULL;CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  qaplet_process_options o;prepare(&o,"--empty");o.flags=QAPLET_PROCESS_REPLACE_ENV;
  qaplet_process p=QAPLET_PROCESS_INIT;qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK && !observed_exit(r,&p,&status));
  qaplet_process old=p;CHECK(!completed(r,&p,37));
  CHECK(spawn_ok(r,&o,&p,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_process_get_status(r,&old,&status,NULL)==QAPLET_INVALID_HANDLE && qaplet_process_get_status(other,&p,&status,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(!completed(r,&p,37) && !finish(r) && !finish(other));
  puts("M2 process identity: one-slot reuse; stale generation and cross-Runtime tokens refused; current child unaffected: pass");return 0;
}
typedef struct {
  qaplet_runtime *runtime,*other;
  qaplet_process process,stale;
  _Atomic int ready,error,notifications,notify_error;
} process_observer;
static void query_notify(void *opaque) {
  process_observer *o=opaque;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  /* Deliberately violate the HostNotify rule to verify its TLS guard also
   * rejects the otherwise thread-safe POD entry before acquiring a lock. */
  if (qaplet_get_process_status(o->runtime,&o->process,&status,NULL)!=QAPLET_BUSY)
    o->notify_error=1;
  ++o->notifications;
}
static void *query_running(void *opaque) {
  process_observer *o=opaque;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  if (qaplet_get_process_status(o->runtime,&o->process,&status,NULL)!=QAPLET_OK ||
      status.exited || status.reaped || status.delivered || !status.referenced || !status.helper_active)
    o->error=1;
  qaplet_process_status unchanged=status;
  if (qaplet_process_get_status(o->runtime,&o->process,&status,NULL)!=QAPLET_WRONG_THREAD ||
      qaplet_get_process_status(o->other,&o->process,&status,NULL)!=QAPLET_INVALID_HANDLE ||
      qaplet_get_process_status(o->runtime,&o->stale,&status,NULL)!=QAPLET_INVALID_HANDLE ||
      memcmp(&status,&unchanged,sizeof(status))) o->error=1;
  return NULL;
}
static void *query_released(void *opaque) {
  process_observer *o=opaque;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  if (qaplet_get_process_status(o->runtime,&o->process,&status,NULL)!=QAPLET_OK ||
      !status.reaped || !status.delivered || status.referenced) { o->error=1; o->ready=1; return NULL; }
  o->ready=1;
  double until=milliseconds()+5000;
  qaplet_code code;
  do {
    qaplet_process_status previous=status;
    code=qaplet_get_process_status(o->runtime,&o->process,&status,NULL);
    if (code==QAPLET_INVALID_HANDLE) {
      if (memcmp(&previous,&status,sizeof(status))) o->error=1;
      return NULL;
    }
    if (code!=QAPLET_OK || !status.reaped || !status.delivered || status.referenced) { o->error=1; return NULL; }
    sched_yield();
  } while (milliseconds()<until);
  o->error=1; return NULL;
}
static int query(void) {
  qaplet_config b=QAPLET_CONFIG_INIT; qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;
  m.limits.processes=1;
  qaplet_runtime *r=NULL,*other=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK && qaplet_create_m2(&b,&m,&other,NULL)==QAPLET_OK);
  process_observer observer={0};observer.runtime=r;observer.other=other;
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=query_notify;hook.userdata=&observer;
  CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK);
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  CHECK(qaplet_eval(r,"42",2,&progress,NULL)==QAPLET_OK);
  qaplet_process p=QAPLET_PROCESS_INIT;int gate=hanging(r,&p);CHECK(gate>=0);
  observer.process=p;observer.stale=p;++observer.stale.generation;
  pthread_t thread;CHECK(!pthread_create(&thread,NULL,query_running,&observer) && !pthread_join(thread,NULL) && !observer.error);
  CHECK(!close(gate));
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  double until=milliseconds()+5000;
  do {
    CHECK(qaplet_get_process_status(r,&p,&status,NULL)==QAPLET_OK);
    if (status.reaped) break;
    sched_yield();
  } while(milliseconds()<until);
  CHECK(status.exited && status.reaped && status.native_exit_code==37 && !status.code &&
        !status.delivered && status.referenced && status.helper_active);
  qaplet_process_snapshot snapshot=QAPLET_PROCESS_SNAPSHOT_INIT;
  CHECK(qaplet_get_process_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.referenced==1 &&
        !snapshot.completed && snapshot.helper_threads==1);
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_get_result(r,&result,NULL)==QAPLET_OK && result.number==42);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(r,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
  CHECK(!observed_exit(r,&p,&status));
  CHECK(!pthread_create(&thread,NULL,query_released,&observer));
  while (!observer.ready) sched_yield();
  CHECK(qaplet_process_release(r,&p,NULL)==QAPLET_OK && !pthread_join(thread,NULL) && !observer.error);
  qaplet_process_options options;prepare(&options,"--empty");options.flags=QAPLET_PROCESS_REPLACE_ENV;
  qaplet_process next=QAPLET_PROCESS_INIT;
  CHECK(spawn_ok(r,&options,&next,&status,NULL)==QAPLET_OK);
  CHECK(qaplet_get_process_status(r,&p,&status,NULL)==QAPLET_INVALID_HANDLE &&
        qaplet_get_process_status(r,&next,&status,NULL)==QAPLET_OK);
  CHECK(!completed(r,&next,37));
  CHECK(qaplet_close(r,NULL)==QAPLET_OK);
  CHECK(qaplet_get_process_status(r,&next,&status,NULL)==QAPLET_INVALID_HANDLE);
  CHECK(!finish(r) && !finish(other));
  CHECK(observer.notifications && !observer.notify_error);
  puts("M2 process POD query: any-thread token status; no collection/delivery/JS; Runtime/generation/release isolation; HostNotify BUSY; closed cleanup: pass");return 0;
}
static int child(int argc,char **argv) {
  if (argc<4) return 98;
  const char *mode=argv[2];
  if (!strcmp(mode,"--inherit")) {
    errno=0; int flags=fcntl(197,F_GETFD),cached_error=errno; char probe;
    errno=0; int observed=read(197,&probe,1),native_error=errno;
    if (flags!=-1 || cached_error!=EBADF || observed!=-1 || native_error!=EBADF) {
      fprintf(stderr,"inherited descriptor: flags=%d errno=%d read=%d errno=%d\n",flags,cached_error,observed,native_error); return 98;
    }
    const unsigned char expected[]={'I',0,255,'N','\n'},errors[]={'E',0,254,'R'};
    unsigned char data[16]; ssize_t n=read(0,data,sizeof(data));
    if (n!=sizeof(expected) || memcmp(data,expected,sizeof(expected)) || read(0,data,sizeof(data))!=0) return 98;
    return write(1,expected,sizeof(expected))==sizeof(expected) && write(2,errors,sizeof(errors))==sizeof(errors) ? 37 : 98;
  }
  if (!strcmp(mode,"--hang")) { CHECK(write(1,"R",1)==1); char byte; while (read(0,&byte,1)>0) {} return 37; }
  if (!strcmp(mode,"--empty")) { extern char **environ; return environ[0] ? 98 : 37; }
  if (!strcmp(mode,"--snapshot")) return getenv("QAPLET_SNAPSHOT") && !strcmp(getenv("QAPLET_SNAPSHOT"),"frozen") && !getenv("QAPLET_AFTER_CREATE") ? 37 : 98;
  if (argc!=10) return 98;
  for (int i=0;i<7;++i) if (strcmp(argv[i+3],special[i])) return 98;
  extern char **environ; size_t n=0; while(environ[n]) ++n;
  char actual[PATH_MAX]; char byte;
  return n==2 && getenv("QAPLET_ONLY") && !strcmp(getenv("QAPLET_ONLY"),"完整 值") && getenv("QAPLET_CWD") && getcwd(actual,sizeof(actual)) && !strcmp(actual,getenv("QAPLET_CWD")) && read(0,&byte,1)==0 ? 37 : 98;
}
int sdk_process(int argc,char **argv) {
  if (argc>1 && !strcmp(argv[1],"--m2-process-child")) return child(argc,argv);
  CHECK(argc==4); self=argv[2]; launcher=argv[3];
  char original[PATH_MAX]; CHECK(getcwd(original,sizeof(original)));
  const char *temporary=getenv("TMPDIR"); if (!temporary) temporary=getenv("TEMP"); if (!temporary) temporary="/tmp";
  snprintf(root,sizeof(root),"%s/qaplet-process-参数-XXXXXX",temporary); if (!mkdtemp(root)) { fprintf(stderr,"fixture root=%s windows=%d\n",root,IsWindows()); return 1; } CHECK(realpath(root,cwd));
  CHECK(!chdir(cwd)); CHECK(getcwd(cwd,sizeof(cwd)));
  if (IsWindows()) { char16_t wide[PATH_MAX]; extern uint32_t GetCurrentDirectory(uint32_t,char16_t *); CHECK(GetCurrentDirectory(PATH_MAX,wide)); tprecode16to8(native_cwd,sizeof(native_cwd),wide); }
  else strcpy(native_cwd,cwd);
  CHECK(!chdir(original));
  snprintf(executable,sizeof(executable),"%s/子程序 空格%s",cwd,IsWindows() ? ".exe" : "");
  char source[PATH_MAX];
  if (IsWindows()) { strcpy(source,self); char *slash=strrchr(source,'/'); if (!slash) slash=strrchr(source,'\\'); CHECK(slash); strcpy(slash+1,"sdk/process-child.exe"); }
  else strcpy(source,launcher);
  CHECK(!copy_file(source,executable));
  int result=!strcmp(argv[1],"--m2-process-query") ? query() : !strcmp(argv[1],"--m2-process-inputs") ? inputs() : !strcmp(argv[1],"--m2-process-errors") ? errors() : !strcmp(argv[1],"--m2-process-inherit") ? inherit() : !strcmp(argv[1],"--m2-process-limits") ? limits() : !strcmp(argv[1],"--m2-process-ledger") ? ledger() : !strcmp(argv[1],"--m2-process-scheduling") ? scheduling() : !strcmp(argv[1],"--m2-process-liveness") ? liveness() : !strcmp(argv[1],"--m2-process-identity") ? identity() : cleanup();
  CHECK(!unlink(executable)); CHECK(!rmdir(cwd)); return result;
}
#ifdef SDK_STANDALONE
int main(int argc,char **argv) { return sdk_process(argc,argv); }
#endif
