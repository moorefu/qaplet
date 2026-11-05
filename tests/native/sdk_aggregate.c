/* SPDX-License-Identifier: MIT */
/* Cross-resource contracts use only the distributed public SDK. */
#include "qaplet.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"aggregate public contract line %d: %s\n",__LINE__,#c); return 1; } } while (0)

typedef struct { qaplet_runtime *runtime; unsigned calls,failures; } diagnostic_probe;
static unsigned refused_io(qaplet_runtime *r,qaplet_code expected,int short_prefix) {
  struct { qaplet_diagnostic d; unsigned char canary[32]; } storage;
  memset(&storage,0x5a,sizeof(storage));
  storage.d=(qaplet_diagnostic)QAPLET_DIAGNOSTIC_INIT;
  storage.d.struct_size=short_prefix ? 8 : sizeof(storage.d);
  storage.d.code=QAPLET_HOST_ERROR;storage.d.flags=0x1234;storage.d.length=123;
  unsigned char original[sizeof(storage)];memcpy(original,&storage,sizeof(storage));
  qaplet_io_endpoint e=QAPLET_IO_ENDPOINT_INIT;qaplet_io_operation op=QAPLET_IO_OPERATION_INIT;
  qaplet_io_endpoint_status endpoint=QAPLET_IO_ENDPOINT_STATUS_INIT;qaplet_io_status status=QAPLET_IO_STATUS_INIT;
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;qaplet_io_snapshot snapshot=QAPLET_IO_SNAPSHOT_INIT;
  qaplet_stdio_info info=QAPLET_STDIO_INFO_INIT;qaplet_process process=QAPLET_PROCESS_INIT;
  unsigned failures=0;
#define REFUSAL(call) do { memcpy(&storage,original,sizeof(storage)); qaplet_code c=(call); if(c!=expected || memcmp(&storage,original,sizeof(storage)))++failures; } while(0)
  REFUSAL(qaplet_stdio_inspect(r,0,&info,&storage.d));
  REFUSAL(qaplet_stdio_attach(r,0,&e,&info,&storage.d));
  REFUSAL(qaplet_stdio_get(r,0,&e,&info,&storage.d));
  REFUSAL(qaplet_stdio_detach(r,0,&storage.d));
  REFUSAL(qaplet_process_get_stdio(r,&process,0,&e,&storage.d));
  REFUSAL(qaplet_io_read(r,&e,1,&op,&storage.d));
  REFUSAL(qaplet_io_write(r,&e,"x",1,&op,&storage.d));
  REFUSAL(qaplet_io_finish(r,&e,&storage.d));
  REFUSAL(qaplet_io_close(r,&e,&storage.d));
  REFUSAL(qaplet_io_set_ref(r,&e,0,&storage.d));
  REFUSAL(qaplet_get_io_endpoint_status(r,&e,&endpoint,&storage.d));
  REFUSAL(qaplet_io_cancel(r,&op,&storage.d));
  REFUSAL(qaplet_get_io_status(r,&op,&status,&storage.d));
  REFUSAL(qaplet_io_copy_result(r,&op,&result,&storage.d));
  REFUSAL(qaplet_io_release(r,&op,&storage.d));
  REFUSAL(qaplet_get_io_snapshot(r,&snapshot,&storage.d));
#undef REFUSAL
  return failures;
}
static void forbidden_io_notify(void *opaque) {
  diagnostic_probe *p=opaque;++p->calls;p->failures+=refused_io(p->runtime,QAPLET_BUSY,0);
}
static int diagnostics(void) {
  qaplet_config b=QAPLET_CONFIG_INIT;qaplet_m2_config m=QAPLET_M2_CONFIG_INIT;qaplet_runtime *r=NULL;
  CHECK(qaplet_create_m2(&b,&m,&r,NULL)==QAPLET_OK);
  unsigned short_failures=refused_io(r,QAPLET_ABI_MISMATCH,1);
  diagnostic_probe probe={r,0,0};qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;hook.function=forbidden_io_notify;hook.userdata=&probe;
  CHECK(qaplet_set_host_notify(r,&hook,NULL)==QAPLET_OK && qaplet_wake(r,NULL)==QAPLET_OK && probe.calls);
  qaplet_io_snapshot io=QAPLET_IO_SNAPSHOT_INIT;CHECK(qaplet_get_io_snapshot(r,&io,NULL)==QAPLET_OK && !io.endpoints && !io.operations && !io.data_bytes);
  CHECK(qaplet_close(r,NULL)==QAPLET_OK && qaplet_destroy(r,NULL)==QAPLET_OK);
  if(short_failures || probe.failures)fprintf(stderr,"ordinary IO diagnostic refusal: short=%u notify=%u callbacks=%u; Runtime cleaned\n",short_failures,probe.failures,probe.calls);
  CHECK(!short_failures && !probe.failures);
  puts("M2 aggregate diagnostics: public short-prefix POD/canary and HostNotify refusals preserve caller diagnostics; no resource acceptance: pass");return 0;
}
int sdk_aggregate(const char *mode) {
  if(!strcmp(mode,"--m2-aggregate-diagnostics"))return diagnostics();
  return 2;
}
#ifdef QAPLET_AGGREGATE_STANDALONE
int main(int argc,char **argv){return argc==2 ? sdk_aggregate(argv[1]) : 2;}
#endif
