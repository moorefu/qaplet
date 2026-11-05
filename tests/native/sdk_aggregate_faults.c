/* SPDX-License-Identifier: MIT */
/* Controlled system boundaries plus the shipped public consumer, no private SDK data. */
#include "host-aggregate.c"
#include <errno.h>
#include <poll.h>
static _Atomic int fail_send,fail_clock,fail_wait,allocation_mode,fail_malloc,malloc_refusals;
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t size){
  if(atomic_exchange(&allocation_mode,0)){errno=ENOMEM;return NULL;}return __real_calloc(n,size);
}
void *__real_malloc(size_t);
void *__wrap_malloc(size_t n){if(atomic_exchange(&fail_malloc,0)){atomic_fetch_add(&malloc_refusals,1);errno=ENOMEM;return NULL;}return __real_malloc(n);}
ssize_t __real_send(int,const void *,size_t,int);
ssize_t __wrap_send(int fd,const void *data,size_t n,int flags){if(atomic_exchange(&fail_send,0)){errno=EIO;return -1;}return __real_send(fd,data,n,flags);}
int __real_clock_gettime(clockid_t,struct timespec *);
int __wrap_clock_gettime(clockid_t clock,struct timespec *now){if(clock==CLOCK_MONOTONIC && atomic_exchange(&fail_clock,0)){errno=EIO;return -1;}return __real_clock_gettime(clock,now);}
int __real_poll(struct pollfd *,nfds_t,int);
int __wrap_poll(struct pollfd *fds,nfds_t n,int timeout){if(atomic_exchange(&fail_wait,0)){errno=EIO;return -1;}return __real_poll(fds,n,timeout);}
typedef struct {qaplet_code stop,allocation;unsigned calls;} allocation_stop;
static qaplet_code stop_then_allocation(qaplet_runtime *r,uint64_t call,size_t argc,const qaplet_value *argv,void *opaque){
  (void)argc;(void)argv;allocation_stop *p=opaque;++p->calls;p->stop=qaplet_request_stop(r,NULL);
  unsigned char bytes[8]={0};qaplet_value value=QAPLET_VALUE_INIT;value.type=QAPLET_VALUE_BYTES;value.data=bytes;value.length=sizeof(bytes);
  atomic_store(&fail_malloc,1);p->allocation=qaplet_host_set_result(r,call,&value,NULL);return p->allocation;
}
int main(void){
  for(unsigned mode=0;mode<8;++mode){
    qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
    b.limits.engine_heap_bytes=1048576;if(mode==6)b.limits.execution_timeout_ms=1;
    m.limits.worker_threads=1;m.limits.operations=4;m.limits.operation_data_bytes=32;m.limits.work_input_bytes=4;m.limits.work_output_bytes=4;m.limits.io_block_bytes=8;m.limits.timers=1;m.limits.keepalive_tokens=1;
    CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
    work_gate gate={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0};qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;h.name="gate";h.name_length=4;h.function=gated_work;h.userdata=&gate;h.input_bytes=4;h.output_bytes=4;
    CHECK(qaplet_register_work_handler(r,&h,NULL)==QAPLET_OK);
    allocation_stop ordered={QAPLET_BAD_STATE,QAPLET_BAD_STATE,0};
    if(mode==3){qaplet_host_export item=QAPLET_HOST_EXPORT_INIT;item.name="act";item.name_length=3;item.kind=QAPLET_HOST_FUNCTION;item.function=stop_then_allocation;item.userdata=&ordered;qaplet_host_module module=QAPLET_HOST_MODULE_INIT;module.name="host:order";module.name_length=10;module.exports=&item;module.export_count=1;CHECK(qaplet_register_host_module(r,&module,NULL)==QAPLET_OK);}
    qaplet_timer timer=QAPLET_TIMER_INIT;unsigned timer_calls=0;qaplet_keepalive token=QAPLET_KEEPALIVE_INIT;
    CHECK(qaplet_timer_create(r,60000,never_timer,&timer_calls,&timer,NULL)==QAPLET_OK && qaplet_keepalive_create(r,&token,NULL)==QAPLET_OK);
    qaplet_work_task first=QAPLET_WORK_TASK_INIT,next=QAPLET_WORK_TASK_INIT;CHECK(qaplet_work_submit(r,"gate",4,"copy",4,&first,NULL)==QAPLET_OK);
    pthread_mutex_lock(&gate.mutex);while(!gate.entered)pthread_cond_wait(&gate.condition,&gate.mutex);pthread_mutex_unlock(&gate.mutex);
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_OK);
    qaplet_code expected=QAPLET_CONTROL_FAILURE;
    if(mode==0){expected=QAPLET_EXECUTION_STOPPED;atomic_store(&fail_send,1);CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK && !atomic_load(&fail_send));}
    if(mode==1){atomic_store(&fail_send,1);CHECK(qaplet_work_submit(r,"gate",4,"next",4,&next,NULL)==QAPLET_OK && next.generation && !atomic_load(&fail_send));}
    if(mode==2){expected=QAPLET_MEMORY_LIMIT;atomic_store(&allocation_mode,1);CHECK(qaplet_work_submit(r,"gate",4,"next",4,&next,NULL)==QAPLET_MEMORY_LIMIT && !next.generation);}
    if(mode==3){expected=QAPLET_EXECUTION_STOPPED;const char source[]="import('host:order').then(m=>m.act())";qaplet_progress p=QAPLET_PROGRESS_INIT;qaplet_code c=qaplet_eval(r,source,sizeof(source)-1,&p,NULL);while(c==QAPLET_OK && p.phase==QAPLET_PHASE_PENDING)c=qaplet_pump(r,&p,NULL);CHECK(c==QAPLET_EXECUTION_STOPPED && ordered.calls==1 && ordered.stop==QAPLET_OK && (ordered.allocation==QAPLET_OK || ordered.allocation==QAPLET_MEMORY_LIMIT) && atomic_load(&malloc_refusals)==1 && !atomic_load(&fail_malloc));}
    if(mode==4){atomic_store(&fail_clock,1);CHECK(qaplet_run_once(r,0,&drive,NULL)==QAPLET_CONTROL_FAILURE && !atomic_load(&fail_clock));}
    if(mode==5){atomic_store(&fail_wait,1);CHECK(qaplet_run_once(r,20,&drive,NULL)==QAPLET_CONTROL_FAILURE && !atomic_load(&fail_wait));}
    if(mode==6){expected=QAPLET_DEADLINE_EXCEEDED;qaplet_progress p=QAPLET_PROGRESS_INIT;const char source[]="let end=Date.now()+20;while(Date.now()<end){};42";CHECK(qaplet_eval(r,source,sizeof(source)-1,&p,NULL)==QAPLET_DEADLINE_EXCEEDED);}
    if(mode==7){expected=QAPLET_MEMORY_LIMIT;qaplet_progress p=QAPLET_PROGRESS_INIT;const char source[]="try{let a=[];for(let i=0;i<100000;++i)a.push('secret'.repeat(128))}catch(e){};42";CHECK(qaplet_eval(r,source,sizeof(source)-1,&p,NULL)==QAPLET_MEMORY_LIMIT);}
    CHECK(qaplet_request_stop(r,NULL)==QAPLET_OK);
    qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;qaplet_m2_diagnostic diagnostic=QAPLET_M2_DIAGNOSTIC_INIT;qaplet_work_snapshot work=QAPLET_WORK_SNAPSHOT_INIT;
    CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==expected);
    if(mode<2 || mode==4 || mode==5)CHECK(snapshot.control_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && snapshot.control_error==EIO);
    CHECK(qaplet_get_work_snapshot(r,&work,NULL)==QAPLET_OK && work.accepted==(mode==1 ? 2 : 1));
    CHECK(qaplet_close(r,NULL)==QAPLET_OK);
    if(mode==6){atomic_store(&fail_clock,1);CHECK(qaplet_quiesce(r,1,&drive,NULL)==QAPLET_CONTROL_FAILURE && !atomic_load(&fail_clock));}
    CHECK(qaplet_get_m2_diagnostic(r,&diagnostic,NULL)==QAPLET_OK && diagnostic.primary_reason==expected && qaplet_destroy(r,NULL)==QAPLET_BUSY);
    pthread_mutex_lock(&gate.mutex);gate.release=1;pthread_cond_broadcast(&gate.condition);pthread_mutex_unlock(&gate.mutex);
    double end=milliseconds()+5000;do{qaplet_code c=qaplet_quiesce(r,mode==6 ? 0 : 100,&drive,NULL);CHECK(c==(mode==6 ? QAPLET_CONTROL_FAILURE : QAPLET_OK));if(drive.reason!=QAPLET_DRIVE_QUIESCENT)sched_yield();}while(drive.reason!=QAPLET_DRIVE_QUIESCENT && milliseconds()<end);
    CHECK(drive.reason==QAPLET_DRIVE_QUIESCENT && !timer_calls && gate.entered==1);
    CHECK(qaplet_get_m2_snapshot(r,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_CLOSED && snapshot.termination_reason==expected && !snapshot.engine_current_bytes && !snapshot.native_handles);
    CHECK(qaplet_destroy(r,NULL)==QAPLET_OK && !pthread_cond_destroy(&gate.condition) && !pthread_mutex_destroy(&gate.mutex));
  }
  puts("M2 aggregate controlled faults: eight mixed Work/timer/token cases; stop/control and stop/actual allocation ordered both ways; accepted work survives wake fault; core clock/wait and caught engine allocation terminate; deadline first survives late CLOSED cleanup fault; first cause and real join retained: pass");return 0;
}
