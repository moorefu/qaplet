/* SPDX-License-Identifier: MIT */
/* Controlled OS/allocation seam; consumes only the supplied public SDK. */
#include "qaplet.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"work system seam line %d: %s\n",__LINE__,#c); return 1; } } while (0)
static int fail_allocation, fail_thread, fail_join;
void *__real_calloc(size_t,size_t);
void *__wrap_calloc(size_t n,size_t size) {
  if (fail_allocation) { fail_allocation=0;errno=ENOMEM;return NULL; }
  return __real_calloc(n,size);
}
int __real_pthread_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);
int __wrap_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*function)(void *),void *data) {
  if (fail_thread) { fail_thread=0;return EAGAIN; }
  return __real_pthread_create(thread,attr,function,data);
}
int __real_pthread_tryjoin_np(pthread_t,void **);
int __wrap_pthread_tryjoin_np(pthread_t thread,void **value) {
  if (fail_join) return EIO;
  return __real_pthread_tryjoin_np(thread,value);
}
static qaplet_code copy(qaplet_work_context *context,const void *input,size_t length,
                         qaplet_work_output *output,void *userdata) {
  (void)context;(void)userdata;
  if (length) memcpy(output->data,input,length);
  output->length=length;return QAPLET_OK;
}
static int make(qaplet_runtime **runtime) {
  qaplet_config base=QAPLET_CONFIG_INIT;qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  CHECK(qaplet_create_m2(&base,&config,runtime,NULL)==QAPLET_OK);
  qaplet_work_handler h=QAPLET_WORK_HANDLER_INIT;
  h.name="copy";h.name_length=4;h.function=copy;h.input_bytes=1;h.output_bytes=1;
  CHECK(qaplet_register_work_handler(*runtime,&h,NULL)==QAPLET_OK);return 0;
}
static int finish(qaplet_runtime *runtime) {
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(runtime,100,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_OK);return 0;
}
int main(void) {
  qaplet_runtime *runtime=NULL;qaplet_work_task task=QAPLET_WORK_TASK_INIT;unsigned char input=17;
  CHECK(!make(&runtime));fail_thread=1;
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_HOST_ERROR && !task.runtime_id);
  qaplet_work_snapshot stats=QAPLET_WORK_SNAPSHOT_INIT;
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.accepted && !stats.operations && !stats.worker_threads && stats.start_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && stats.start_error==EAGAIN);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_OPEN && !snapshot.termination_reason);
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_OK);
  qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK && !finish(runtime));
  CHECK(!make(&runtime));task=(qaplet_work_task)QAPLET_WORK_TASK_INIT;fail_allocation=1;
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_MEMORY_LIMIT && !task.runtime_id);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && !stats.operations && !stats.accepted && !stats.worker_threads);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_FAILED && snapshot.termination_reason==QAPLET_MEMORY_LIMIT);
  CHECK(qaplet_request_stop(runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.termination_reason==QAPLET_MEMORY_LIMIT);
  CHECK(!finish(runtime));
  /* Deliberately uncertain join: no fake reclamation or second join attempt.
   * The Runtime/worker storage remains owned through process termination. */
  CHECK(!make(&runtime));
  CHECK(qaplet_work_submit(runtime,"copy",4,&input,1,&task,NULL)==QAPLET_OK);
  CHECK(qaplet_run(runtime,&drive,NULL)==QAPLET_OK);fail_join=1;
  qaplet_code closed=qaplet_close(runtime,NULL);
  CHECK(closed==QAPLET_OK || closed==QAPLET_CONTROL_FAILURE);
  CHECK(qaplet_quiesce(runtime,20,&drive,NULL)==QAPLET_CONTROL_FAILURE && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  qaplet_m2_diagnostic detail=QAPLET_M2_DIAGNOSTIC_INIT;
  CHECK(qaplet_get_m2_diagnostic(runtime,&detail,NULL)==QAPLET_OK && !detail.primary_reason && detail.cleanup_reason==QAPLET_CONTROL_FAILURE && detail.cleanup_error_domain==QAPLET_ERROR_DOMAIN_ERRNO && detail.cleanup_error==EIO && detail.native_state==QAPLET_NATIVE_CLEANING);
  CHECK(qaplet_get_work_snapshot(runtime,&stats,NULL)==QAPLET_OK && stats.worker_threads==1 && stats.operations==1 && stats.data_bytes==2 && stats.handlers==1);
  CHECK(qaplet_get_m2_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.lifecycle==QAPLET_CLOSED && !snapshot.engine_current_bytes && !snapshot.termination_reason);
  CHECK(qaplet_destroy(runtime,NULL)==QAPLET_BUSY);
  CHECK(qaplet_quiesce(runtime,0,&drive,NULL)==QAPLET_CONTROL_FAILURE && drive.reason==QAPLET_DRIVE_WAIT_TIMEOUT);
  puts("controlled M2 work: pthread create errno refuses atomically and recovers; actual calloc refusal permanent; join error independently retained CLOSED/BUSY without false cleanup: pass");
  return 0;
}
