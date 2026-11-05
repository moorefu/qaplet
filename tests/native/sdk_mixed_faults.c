/* SPDX-License-Identifier: MIT */
/* Separate supplied-SDK recipe, interposing only real OS boundaries. */
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static int fault_mode,armed,sockets[16],replacement=-1,notify_pipe[2],gate_entered,gate_release,producer_returned,future_release,failure;
static int fail_initial_socket,observe_wait,expect_same_deadline,wait_pairs;
static struct timespec previous_deadline;
static pthread_mutex_t gate_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition=PTHREAD_COND_INITIALIZER;
static pthread_t producer;
static _Thread_local int stall_notification;
static void mixed_stage(int,qaplet_runtime *);
static void fault_notify(void *);
#define MIXED_FAULT_STAGE(stage,r) mixed_stage(stage,r)
#define MIXED_CLEANUP_CODE (fault_mode==1 ? QAPLET_CONTROL_FAILURE : QAPLET_OK)
#define MIXED_UNKNOWN (fault_mode==1)
#define MIXED_NOTIFY fault_notify
#include "host-mixed.c"

int __real_socket(int,int,int);
int __real_close(int);
ssize_t __real_write(int,const void *,size_t);
int __wrap_socket(int family,int type,int protocol) {
  if(fail_initial_socket) { fail_initial_socket=0;errno=EMFILE;return -1; }
  int fd=__real_socket(family,type,protocol);
  if(fd>=0)for(unsigned i=0;i<16;++i)if(!sockets[i]) { sockets[i]=fd+1;break; }
  return fd;
}
int __real_pthread_cond_timedwait(pthread_cond_t *,pthread_mutex_t *,const struct timespec *);
int __wrap_pthread_cond_timedwait(pthread_cond_t *condition,pthread_mutex_t *mutex,const struct timespec *deadline) {
  if(observe_wait) {
    if(!expect_same_deadline) {
      previous_deadline=*deadline;expect_same_deadline=1;
      /* A permitted native spurious wake cannot start a fresh wait budget. */
      return 0;
    }
    if(deadline->tv_sec!=previous_deadline.tv_sec || deadline->tv_nsec!=previous_deadline.tv_nsec) { failure=1;return EIO; }
    expect_same_deadline=0;++wait_pairs;
  }
  return __real_pthread_cond_timedwait(condition,mutex,deadline);
}
int __wrap_close(int fd) {
  int tracked=-1;for(unsigned i=0;i<16;++i)if(sockets[i]==fd+1) {tracked=i;break;}
  if(armed && tracked>=0) {
    armed=0;sockets[tracked]=0;
    if(__real_close(fd))return -1;
    /* Only the fixture knows close released the original identity. A distinct
     * Host file occupies that number before EINTR is reported to the SDK. */
    int fresh=open("./replacement",O_RDWR|O_CREAT|O_TRUNC,0600);
    if(fresh!=fd) { if(fresh<0 || dup2(fresh,fd)!=fd)failure=1;if(fresh>=0)__real_close(fresh); }
    replacement=fd;errno=EINTR;return -1;
  }
  int code=__real_close(fd);if(!code && tracked>=0)sockets[tracked]=0;return code;
}
ssize_t __wrap_write(int fd,const void *bytes,size_t n) {
  if(stall_notification && fd==notify_pipe[1] && n==1 && *(const char *)bytes=='N') {
    pthread_mutex_lock(&gate_mutex);gate_entered=1;pthread_cond_broadcast(&gate_condition);
    while(!gate_release)pthread_cond_wait(&gate_condition,&gate_mutex);
    pthread_mutex_unlock(&gate_mutex);
  }
  return __real_write(fd,bytes,n);
}
static void fault_notify(void *opaque) {
  notify(opaque);
  if(fault_mode==2 && notify_pipe[1]>0 && write(notify_pipe[1],"N",1)!=1)failure=1;
}
static void *external_producer(void *opaque) {
  qaplet_runtime *r=opaque;stall_notification=1;
  if(qaplet_wake(r,NULL)!=QAPLET_OK)failure=1;
  stall_notification=0;
  pthread_mutex_lock(&gate_mutex);producer_returned=1;pthread_cond_broadcast(&gate_condition);
  while(!future_release)pthread_cond_wait(&gate_condition,&gate_mutex);
  pthread_mutex_unlock(&gate_mutex);
  /* A future external call is still permitted after native QUIESCENT. The Host
   * retains the Runtime/hook/code/userdata until this producer is withdrawn. */
  if(qaplet_wake(r,NULL)!=QAPLET_OK)failure=1;
  return NULL;
}
static void mixed_stage(int stage,qaplet_runtime *r) {
  if(stage==1 && fault_mode==1)armed=1;
  if(stage==2 && fault_mode==2) {
    if(pipe(notify_pipe) || pthread_create(&producer,NULL,external_producer,r))abort();
    pthread_mutex_lock(&gate_mutex);while(!gate_entered)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);
    qaplet_notify_snapshot n=QAPLET_NOTIFY_SNAPSHOT_INIT;
    if(qaplet_get_notify_snapshot(r,&n,NULL)!=QAPLET_OK || n.inflight!=1)abort();
  }
  if(stage==2 && fault_mode==4)observe_wait=1;
  if(stage==3 && fault_mode==4) { observe_wait=0;if(failure || expect_same_deadline || wait_pairs!=2)abort(); }
  if(stage==3 && fault_mode==2) {
    qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;qaplet_work_snapshot w=QAPLET_WORK_SNAPSHOT_INIT;qaplet_notify_snapshot n=QAPLET_NOTIFY_SNAPSHOT_INIT;
    double until=milliseconds()+5000;
    do { if(qaplet_quiesce(r,0,&d,NULL)!=QAPLET_OK || qaplet_get_work_snapshot(r,&w,NULL)!=QAPLET_OK)abort();if(!w.worker_threads)break; }while(milliseconds()<until);
    if(w.worker_threads || w.operations || d.reason!=QAPLET_DRIVE_WAIT_TIMEOUT || qaplet_destroy(r,NULL)!=QAPLET_BUSY || qaplet_get_notify_snapshot(r,&n,NULL)!=QAPLET_OK || n.inflight!=1)abort();
    pthread_mutex_lock(&gate_mutex);gate_release=1;pthread_cond_broadcast(&gate_condition);while(!producer_returned)pthread_cond_wait(&gate_condition,&gate_mutex);pthread_mutex_unlock(&gate_mutex);
  }
  if(stage==4) {
    qaplet_m2_diagnostic detail=QAPLET_M2_DIAGNOSTIC_INIT;
    if(qaplet_get_m2_diagnostic(r,&detail,NULL)!=QAPLET_OK || detail.primary_reason!=QAPLET_EXECUTION_STOPPED || detail.cleanup_reason!=MIXED_CLEANUP_CODE)abort();
    if(fault_mode==1) {
      if(failure || detail.native_uncertain_handles!=1 || replacement<0 || fcntl(replacement,F_GETFD)<0 || write(replacement,"safe",4)!=4)abort();
      for(unsigned i=0;i<3;++i) { qaplet_drive_result d=QAPLET_DRIVE_RESULT_INIT;if(qaplet_quiesce(r,0,&d,NULL)!=QAPLET_CONTROL_FAILURE || d.reason!=QAPLET_DRIVE_WAIT_TIMEOUT || fcntl(replacement,F_GETFD)<0)abort(); }
      if(__real_close(replacement) || unlink("./replacement"))abort();
    } else if(fault_mode==2) {
      pthread_mutex_lock(&gate_mutex);future_release=1;pthread_cond_broadcast(&gate_condition);pthread_mutex_unlock(&gate_mutex);
      if(pthread_join(producer,NULL) || failure)abort();
      qaplet_notify_snapshot n=QAPLET_NOTIFY_SNAPSHOT_INIT;
      if(qaplet_get_notify_snapshot(r,&n,NULL)!=QAPLET_OK || n.inflight || n.inflight_peak<2)abort();
      if(close(notify_pipe[0]) || close(notify_pipe[1]))abort();
    }
  }
}
int main(int argc,char **argv) {
  if(argc==2 && !strcmp(argv[1],"--m2-mixed-child"))return sdk_mixed(argc,argv);
  fault_mode=argc>1 && strstr(argv[1],"uncertain") ? 1 : argc>1 && strstr(argv[1],"initialization") ? 3 : argc>1 && strstr(argv[1],"wait-budget") ? 4 : 2;
  if(fault_mode==3) {
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=(void *)1;
    qaplet_m2_diagnostic detail=QAPLET_M2_DIAGNOSTIC_INIT;fail_initial_socket=1;
    if(qaplet_create_m2_with_diagnostic(&b,&m,&r,&detail,NULL)!=QAPLET_CONTROL_FAILURE || r || detail.primary_reason!=QAPLET_CONTROL_FAILURE || detail.creation_rollback!=QAPLET_ROLLBACK_COMPLETE || detail.native_uncertain_handles)abort();
  }
  return sdk_mixed(argc,argv);
}
