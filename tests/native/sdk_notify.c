/* SPDX-License-Identifier: MIT */
/* Public Host adapter: its socket pair and wait belong to the Host. */
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdatomic.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"HostNotify line %d: %s\n",__LINE__,#c); return 1; } } while (0)
typedef struct {
  qaplet_runtime *runtime;
  qaplet_keepalive token;
  int receive, transmit;
  _Atomic int error;
} host_loop;
static int nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && !fcntl(fd, F_SETFL, flags | O_NONBLOCK) && !fcntl(fd, F_SETFD, FD_CLOEXEC);
}
static int pair(host_loop *host) {
  int listener = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in address = {0}; socklen_t length = sizeof(address);
  address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  CHECK(listener >= 0 && !bind(listener,(void *)&address,length));
  CHECK(!getsockname(listener,(void *)&address,&length) && !listen(listener,1));
  host->transmit = socket(AF_INET,SOCK_STREAM,0);
  CHECK(host->transmit >= 0 && !connect(host->transmit,(void *)&address,length));
  host->receive = accept(listener,NULL,NULL);
  CHECK(host->receive >= 0 && !close(listener));
  CHECK(nonblocking(host->receive) && nonblocking(host->transmit));
  return 0;
}
static void notify(void *userdata) {
  host_loop *host = userdata;
  /* No SDK, JS, lock or retry: a full Host channel already contains a hint. */
  ssize_t sent = send(host->transmit,"H",1,MSG_NOSIGNAL);
  if (sent != 1 && errno != EAGAIN && errno != EWOULDBLOCK) host->error = 1;
}
static int wait_host(host_loop *host) {
  struct pollfd fd = {host->receive,POLLIN,0};
  CHECK(poll(&fd,1,3000) == 1 && (fd.revents & POLLIN));
  char bytes[256]; ssize_t got;
  do { got = recv(host->receive,bytes,sizeof(bytes),0); } while (got > 0 || (got < 0 && errno == EINTR));
  CHECK(got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
  return 0;
}
static void *events(void *userdata) {
  host_loop *host = userdata;
  for (unsigned i=0;i<128;++i) {
    char payload='x';
    if (qaplet_post_event(host->runtime,i,i,&payload,1,NULL) != QAPLET_OK) host->error=1;
    payload=0;
  }
  if (qaplet_keepalive_release(host->runtime,&host->token,NULL) != QAPLET_OK ||
      qaplet_wake(host->runtime,NULL) != QAPLET_OK) host->error=1;
  return NULL;
}
static void ignore_notification(void *userdata) { (void)userdata; }
static void *release_waiter(void *userdata) {
  host_loop *host=userdata;
  qaplet_m2_snapshot snapshot=QAPLET_M2_SNAPSHOT_INIT;
  do {
    if(qaplet_get_m2_snapshot(host->runtime,&snapshot,NULL)!=QAPLET_OK) { host->error=1; return NULL; }
  } while(!snapshot.driving || !snapshot.wait_calls);
  if(qaplet_keepalive_release(host->runtime,&host->token,NULL)!=QAPLET_OK) host->error=1;
  return NULL;
}
static void *stop_waiter(void *userdata) {
  host_loop *host=userdata;
  if(qaplet_request_stop(host->runtime,NULL)!=QAPLET_OK) host->error=1;
  return NULL;
}
static int finish(host_loop *host) {
  qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  CHECK(qaplet_close(host->runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_quiesce(host->runtime,100,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_QUIESCENT);
  CHECK(qaplet_get_m2_snapshot(host->runtime,&snapshot,NULL)==QAPLET_OK &&
    !snapshot.engine_current_bytes && !snapshot.native_handles && !snapshot.native_uncertain_handles &&
    !snapshot.keepalive_tokens && !snapshot.reactor_registrations);
  CHECK(qaplet_destroy(host->runtime,NULL)==QAPLET_OK);
  CHECK(!close(host->receive) && !close(host->transmit) && !host->error);
  return 0;
}
static int host_events(void) {
  host_loop host = {0};
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_host_notify hook = QAPLET_HOST_NOTIFY_INIT;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(!pair(&host));
  CHECK(qaplet_create_m2(&base,&config,&host.runtime,NULL)==QAPLET_OK);
  hook.function=notify; hook.userdata=&host;
  CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_OK);
  /* Registration copies the declaration, never borrows this stack object. */
  hook.function=NULL; hook.userdata=NULL;
  qaplet_notify_snapshot notifications = QAPLET_NOTIFY_SNAPSHOT_INIT;
  CHECK(qaplet_get_notify_snapshot(host.runtime,&notifications,NULL)==QAPLET_OK && notifications.installed && !notifications.inflight);
  const char source[]="globalThis.seen=0;import('qaplet:events').then(m=>m.onEvent(e=>{if(e.type!==seen||e.payload[0]!==120||e.token!==BigInt(e.type))throw Error();seen++;}))";
  CHECK(qaplet_eval(host.runtime,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(host.runtime,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_keepalive_create(host.runtime,&host.token,NULL)==QAPLET_OK);
  pthread_t producer; CHECK(!pthread_create(&producer,NULL,events,&host));
  CHECK(!wait_host(&host)); CHECK(!pthread_join(producer,NULL) && !host.error);
  CHECK(qaplet_get_snapshot(host.runtime,&snapshot,NULL)==QAPLET_OK && snapshot.events_accepted==128 && !snapshot.events_consumed);
  do { CHECK(qaplet_pump(host.runtime,&progress,NULL)==QAPLET_OK); }
  while (progress.progressed);
  CHECK(qaplet_eval(host.runtime,"seen",4,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_result(host.runtime,&result,NULL)==QAPLET_OK && result.number==128);
  CHECK(qaplet_get_snapshot(host.runtime,&snapshot,NULL)==QAPLET_OK && snapshot.events_consumed==128 && !snapshot.queue_depth);
  CHECK(!finish(&host));
  puts("M2 HostNotify: real Host socket wait; producer hints; copied FIFO; coalesced and redundant hints preserve 128 events; owner pump; joined cleanup: pass");
  return 0;
}
static int controls(void) {
  for(int internal=0;internal<2;++internal) {
    host_loop host={0}; qaplet_config base=QAPLET_CONFIG_INIT;
    qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
    qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;
    qaplet_drive_result drive=QAPLET_DRIVE_RESULT_INIT;
    CHECK(!pair(&host));
    CHECK(qaplet_create_m2(&base,&config,&host.runtime,NULL)==QAPLET_OK);
    hook.function=internal?ignore_notification:notify; hook.userdata=&host;
    CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_OK);
    CHECK(qaplet_keepalive_create(host.runtime,&host.token,NULL)==QAPLET_OK);
    pthread_t producer;
    CHECK(!pthread_create(&producer,NULL,internal?release_waiter:stop_waiter,&host));
    if(internal) CHECK(qaplet_run_once(host.runtime,3000,&drive,NULL)==QAPLET_OK && drive.reason==QAPLET_DRIVE_IDLE);
    else {
      CHECK(!wait_host(&host));
      CHECK(qaplet_run_once(host.runtime,0,&drive,NULL)==QAPLET_EXECUTION_STOPPED && drive.reason==QAPLET_DRIVE_STOPPED);
    }
    CHECK(!pthread_join(producer,NULL) && !host.error);
    CHECK(!finish(&host));
  }
  puts("M2 HostNotify controls: cross-thread stop wakes Host; silent hook cannot prevent internal wait/token release; independent instances; cleanup: pass");
  return 0;
}
static void *foreign_registration(void *userdata) {
  host_loop *host=userdata;
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;
  hook.function=notify; hook.userdata=host;
  qaplet_notify_snapshot snapshot=QAPLET_NOTIFY_SNAPSHOT_INIT;
  if(qaplet_set_host_notify(host->runtime,&hook,NULL)!=QAPLET_WRONG_THREAD ||
     qaplet_get_notify_snapshot(host->runtime,&snapshot,NULL)!=QAPLET_OK) host->error=1;
  return NULL;
}
static int registration(void) {
  host_loop host={0}; qaplet_config base=QAPLET_CONFIG_INIT;
  qaplet_m2_config config=QAPLET_M2_CONFIG_INIT;
  qaplet_host_notify hook=QAPLET_HOST_NOTIFY_INIT;
  qaplet_notify_snapshot snapshot=QAPLET_NOTIFY_SNAPSHOT_INIT;
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  qaplet_abi abi=QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK && (abi.capabilities&QAPLET_CAP_HOST_NOTIFY));
  CHECK(qaplet_create(&base,&host.runtime,NULL)==QAPLET_OK);
  hook.function=ignore_notification;
  CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_UNSUPPORTED);
  CHECK(qaplet_get_notify_snapshot(host.runtime,&snapshot,NULL)==QAPLET_UNSUPPORTED);
  CHECK(qaplet_close(host.runtime,NULL)==QAPLET_OK && qaplet_destroy(host.runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_create_m2(&base,&config,&host.runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_get_notify_snapshot(host.runtime,&snapshot,NULL)==QAPLET_OK && !snapshot.installed && !snapshot.inflight_peak);
  CHECK(qaplet_set_host_notify(host.runtime,NULL,NULL)==QAPLET_INVALID_ARGUMENT);
  hook.struct_size=8; CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_ABI_MISMATCH);
  hook=(qaplet_host_notify)QAPLET_HOST_NOTIFY_INIT;
  CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_INVALID_ARGUMENT);
  hook.function=ignore_notification;
  hook.flags=1; CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_INVALID_ARGUMENT); hook.flags=0;
  hook.reserved[1]=1; CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_INVALID_ARGUMENT); hook.reserved[1]=0;
  struct { qaplet_host_notify hook; uint64_t tail; } extended={QAPLET_HOST_NOTIFY_INIT,1};
  extended.hook.function=ignore_notification; extended.hook.struct_size=sizeof(extended);
  CHECK(qaplet_set_host_notify(host.runtime,&extended.hook,NULL)==QAPLET_ABI_MISMATCH);
  pthread_t foreign; CHECK(!pthread_create(&foreign,NULL,foreign_registration,&host));
  CHECK(!pthread_join(foreign,NULL) && !host.error);
  extended.tail=0;
  CHECK(qaplet_set_host_notify(host.runtime,&extended.hook,NULL)==QAPLET_OK);
  CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_BAD_STATE);
  snapshot.struct_size=8; CHECK(qaplet_get_notify_snapshot(host.runtime,&snapshot,NULL)==QAPLET_ABI_MISMATCH);
  CHECK(qaplet_close(host.runtime,NULL)==QAPLET_OK && qaplet_destroy(host.runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_create_m2(&base,&config,&host.runtime,NULL)==QAPLET_OK);
  CHECK(qaplet_eval(host.runtime,"42",2,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_set_host_notify(host.runtime,&hook,NULL)==QAPLET_BAD_STATE);
  CHECK(qaplet_close(host.runtime,NULL)==QAPLET_OK && qaplet_destroy(host.runtime,NULL)==QAPLET_OK);
  puts("M2 HostNotify registration: M1 disabled; copied one-time declaration; ABI/tails/flags; owner thread; execution freeze; independent POD: pass");
  return 0;
}
int sdk_notify(const char *mode) {
  if(!strcmp(mode,"--m2-notify")) return host_events();
  if(!strcmp(mode,"--m2-notify-controls")) return controls();
  if(!strcmp(mode,"--m2-notify-registration")) return registration();
  return 2;
}
