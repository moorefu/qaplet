/* SPDX-License-Identifier: MIT */
/* Package-only C Host: qaplet.h plus the fixed CRT's native system headers.
 * Build on Ubuntu with the delivered public header and both SDK archives.
 * Run: application-m2 MODE SELF LAUNCHER FILE
 * MODE: --normal, --file-error, --cancel or --cleanup-pending.
 * SELF is an explicit executable path. LAUNCHER is an explicit APE loader on
 * Linux, or "-" to execute SELF directly on Windows. No shell or PATH search.
 * FILE belongs to the caller; this example never writes a global fixture. */
#define _COSMO_SOURCE 1
#include "qaplet.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"
#include "libc/nt/runtime.h"

enum { FILE_BYTES = 256, PATH_BYTES = 4096 };
typedef struct {
  qaplet_runtime *runtime;
  qaplet_keepalive keepalive;
  int receive, transmit;
  _Atomic int notify_error, producer_error;
  pthread_t producer;
  int producer_started, producer_joined;
  pthread_mutex_t gate_mutex;
  pthread_cond_t gate_condition;
  int gate_initialized, gated, entered, released;
  qaplet_work_task task;
  qaplet_work_result work;
  unsigned char file[FILE_BYTES];
  int work_done;
  qaplet_process process;
  qaplet_process_status child;
  qaplet_io_endpoint streams[3];
  qaplet_io_operation operations[3];
  int reading[2], eof[2], input_started, writing;
  unsigned char output[2][FILE_BYTES];
  size_t output_length[2], written;
} application;

/* The explicit cancellation demonstration orders acceptance while a worker is
 * genuinely in flight. Normal file work never waits at this Host gate. */
static int wait_entered(application *app) {
  struct timespec until;
  if (clock_gettime(CLOCK_REALTIME, &until)) return 0;
  until.tv_sec += 3;
  pthread_mutex_lock(&app->gate_mutex);
  int error = 0;
  while (!app->entered && !error)
    error = pthread_cond_timedwait(&app->gate_condition, &app->gate_mutex, &until);
  pthread_mutex_unlock(&app->gate_mutex);
  return !error;
}

static void release_gate(application *app) {
  if (!app->gate_initialized) return;
  pthread_mutex_lock(&app->gate_mutex);
  app->released = 1;
  pthread_cond_broadcast(&app->gate_condition);
  pthread_mutex_unlock(&app->gate_mutex);
}

static double milliseconds(void) {
  struct timespec time;
  if (clock_gettime(CLOCK_MONOTONIC, &time)) return -1;
  return time.tv_sec * 1000.0 + time.tv_nsec / 1000000.0;
}

static int nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL);
  return flags >= 0 && !fcntl(fd, F_SETFL, flags | O_NONBLOCK) &&
    !fcntl(fd, F_SETFD, FD_CLOEXEC);
}

/* Same Host-owned connected channel as the public HostNotify consumer. It is
 * not an SDK wait descriptor and never carries business data. */
static int open_channel(application *app) {
  int listener = socket(AF_INET, SOCK_STREAM, 0), ok = 0;
  struct sockaddr_in address = {0};
  socklen_t length = sizeof(address);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (listener < 0 || bind(listener, (void *)&address, length) ||
      getsockname(listener, (void *)&address, &length) || listen(listener, 1)) goto done;
  app->transmit = socket(AF_INET, SOCK_STREAM, 0);
  if (app->transmit < 0 || connect(app->transmit, (void *)&address, length)) goto done;
  app->receive = accept(listener, NULL, NULL);
  ok = app->receive >= 0 && nonblocking(app->receive) && nonblocking(app->transmit);
done:
  if (listener >= 0 && close(listener)) ok = 0;
  return ok;
}

static void notify(void *userdata) {
  application *app = userdata;
  /* No SDK, JS, locks or retries. A full nonblocking channel already has a
   * hint. The SDK's internal wake mechanism does not depend on this hook. */
  ssize_t sent = send(app->transmit, "H", 1, MSG_NOSIGNAL);
  if (sent != 1 && errno != EAGAIN && errno != EWOULDBLOCK)
    atomic_store(&app->notify_error, 1);
}

static int host_wait(application *app, int timeout) {
  struct pollfd fd = {app->receive, POLLIN, 0};
  int ready;
  do { ready = poll(&fd, 1, timeout); } while (ready < 0 && errno == EINTR);
  if (ready < 0 || (ready && !(fd.revents & POLLIN))) return -1;
  if (ready) {
    char hints[128];
    ssize_t count;
    do { count = recv(app->receive, hints, sizeof(hints), 0); }
    while (count > 0 || (count < 0 && errno == EINTR));
    if (count >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) return -1;
  }
  return ready;
}

static void *produce(void *userdata) {
  application *app = userdata;
  unsigned char event[] = {0, 255, 17};
  if (qaplet_post_event(app->runtime, 23, 42, event, sizeof(event), NULL) != QAPLET_OK)
    atomic_store(&app->producer_error, 1);
  memset(event, '?', sizeof(event)); /* Accepted payload has its own copy. */
  if (qaplet_keepalive_release(app->runtime, &app->keepalive, NULL) != QAPLET_OK)
    atomic_store(&app->producer_error, 1);
  return NULL; /* No future SDK calls after the owner joins this producer. */
}

/* Reads only a bounded prefix of a caller-owned regular file. No JS enters the
 * worker. Cancellation is cooperative; no arbitrary filesystem call is killed.
 * O_NONBLOCK avoids waiting in a FIFO open before rejecting nonregular input. */
static qaplet_code read_file(qaplet_work_context *context, const void *input,
                            size_t length, qaplet_work_output *output, void *userdata) {
  application *app = userdata;
  if (!length || ((const char *)input)[length - 1]) return QAPLET_INVALID_ARGUMENT;
  if (app->gated) {
    pthread_mutex_lock(&app->gate_mutex);
    app->entered = 1;
    pthread_cond_broadcast(&app->gate_condition);
    while (!app->released) pthread_cond_wait(&app->gate_condition, &app->gate_mutex);
    pthread_mutex_unlock(&app->gate_mutex);
  }
  if (qaplet_work_cancelled(context)) return QAPLET_CANCELLED;
  int fd = open(input, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (fd < 0) goto failed_open;
  struct stat info;
  int error = fstat(fd, &info) ? errno : (!S_ISREG(info.st_mode) ? EINVAL : 0);
  qaplet_code code = QAPLET_OK;
  while (!error && output->length < output->capacity) {
    if (qaplet_work_cancelled(context)) { code = QAPLET_CANCELLED; break; }
    ssize_t count = read(fd, (char *)output->data + output->length,
                         output->capacity - output->length);
    if (count > 0) output->length += (size_t)count;
    else if (!count) break;
    else if (errno != EINTR) error = errno;
  }
  if (close(fd) && !error) error = errno;
  if (error) {
    output->error_domain = QAPLET_ERROR_DOMAIN_ERRNO;
    output->error = error;
    return QAPLET_HOST_ERROR;
  }
  return qaplet_work_cancelled(context) ? QAPLET_CANCELLED : code;
failed_open:
  output->error_domain = QAPLET_ERROR_DOMAIN_ERRNO;
  output->error = errno;
  return QAPLET_HOST_ERROR;
}

static void hex(const unsigned char *bytes, size_t length) {
  for (size_t i = 0; i < length; ++i) printf("%02x", bytes[i]);
}

/* Independent POD queries, each synchronized by the SDK. Separate calls are
 * not an atomic combined sample; operation data is a subset of native bytes,
 * and requested thread stack bytes are neither SDK heap nor process RSS. */
static qaplet_code snapshot(application *app, const char *stage) {
  qaplet_m2_snapshot m = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_resource_snapshot r = QAPLET_RESOURCE_SNAPSHOT_INIT;
  qaplet_m2_diagnostic d = QAPLET_M2_DIAGNOSTIC_INIT;
  qaplet_code code = qaplet_get_m2_snapshot(app->runtime, &m, NULL);
  if (!code) code = qaplet_get_resource_snapshot(app->runtime, &r, NULL);
  if (!code) code = qaplet_get_m2_diagnostic(app->runtime, &d, NULL);
  if (code) return code;
  printf("{\"stage\":\"%s\",\"lifecycle\":%u,\"phase\":%u,\"native_state\":%u,"
         "\"termination_reason\":%u,\"cleanup_reason\":%u,\"control_error\":%d,"
         "\"native_bytes\":%" PRIu64 ",\"handles\":%" PRIu64 ",\"uncertain_handles\":%" PRIu64 ","
         "\"operations\":%" PRIu64 ",\"operations_peak\":%" PRIu64 ","
         "\"operation_data_bytes\":%" PRIu64 ",\"io_endpoints\":%" PRIu64 ","
         "\"helpers\":%" PRIu64 ",\"helpers_peak\":%" PRIu64 ",\"helper_stack_bytes\":%" PRIu64 "}\n",
         stage, m.lifecycle, m.phase, m.native_state, m.termination_reason,
         d.cleanup_reason, d.control_error, m.native_current_bytes, m.native_handles,
         m.native_uncertain_handles, r.operations, r.operations_peak, r.operation_data_bytes,
         r.io_endpoints, r.helper_threads, r.helper_threads_peak, r.helper_stack_bytes);
  return QAPLET_OK;
}

static qaplet_code collect_work(application *app) {
  if (app->work_done) return QAPLET_OK;
  qaplet_work_status status = QAPLET_WORK_STATUS_INIT;
  qaplet_code code = qaplet_get_work_status(app->runtime, &app->task, &status, NULL);
  if (code || status.state < QAPLET_WORK_SUCCEEDED || status.physical_pending || status.pending_handoff)
    return code;
  app->work.buffer = app->file;
  app->work.capacity = sizeof(app->file);
  code = qaplet_work_copy_result(app->runtime, &app->task, &app->work, NULL);
  if (!code) code = qaplet_work_release(app->runtime, &app->task, NULL);
  if (!code) app->work_done = 1;
  return code;
}

static qaplet_code collect_stream(application *app, int index) {
  if (!app->reading[index]) return QAPLET_OK;
  qaplet_io_status status = QAPLET_IO_STATUS_INIT;
  qaplet_io_operation *operation = &app->operations[index + 1];
  qaplet_code code = qaplet_get_io_status(app->runtime, operation, &status, NULL);
  /* A completed Windows pipe operation can still own its helper until join.
   * Reuse the endpoint only after that public responsibility retires, keeping
   * process + stdin/stdout/stderr inside the unchanged default of 4 helpers. */
  if (code || status.state == QAPLET_IO_PENDING || status.physical_pending || status.pending_handoff || status.helper_active)
    return code;
  unsigned char bytes[FILE_BYTES];
  qaplet_io_result result = QAPLET_IO_RESULT_INIT;
  result.buffer = bytes;
  result.capacity = sizeof(bytes);
  code = qaplet_io_copy_result(app->runtime, operation, &result, NULL);
  if (code) return code;
  if (result.code) return result.code;
  if (result.length > sizeof(app->output[index]) - app->output_length[index]) return QAPLET_VALUE_TOO_LARGE;
  memcpy(app->output[index] + app->output_length[index], bytes, result.length);
  app->output_length[index] += result.length;
  app->eof[index] = result.eof;
  code = qaplet_io_release(app->runtime, operation, NULL);
  if (!code && !result.eof)
    code = qaplet_io_read(app->runtime, &app->streams[index + 1], sizeof(bytes), operation, NULL);
  if (!code && result.eof) app->reading[index] = 0;
  return code;
}

static qaplet_code input(application *app) {
  qaplet_code code = QAPLET_OK;
  if (app->work_done && !app->input_started) {
    /* write copies the complete block before acceptance; app->file still lives
     * through physical completion. finish is distinct from immediate close. */
    if (!app->work.code && app->work.length) {
      code = qaplet_io_write(app->runtime, &app->streams[0], app->file, app->work.length,
                            &app->operations[0], NULL);
      if (!code) app->writing = 1;
    }
    if (!code) code = qaplet_io_finish(app->runtime, &app->streams[0], NULL);
    if (!code) app->input_started = 1;
  }
  if (!code && app->writing) {
    qaplet_io_status status = QAPLET_IO_STATUS_INIT;
    code = qaplet_get_io_status(app->runtime, &app->operations[0], &status, NULL);
    if (!code && status.state != QAPLET_IO_PENDING && !status.physical_pending &&
        !status.pending_handoff && !status.helper_active) {
      qaplet_io_result result = QAPLET_IO_RESULT_INIT;
      code = qaplet_io_copy_result(app->runtime, &app->operations[0], &result, NULL);
      if (!code) {
        app->written = result.length; /* Preserve actual prefix even on error. */
        code = qaplet_io_release(app->runtime, &app->operations[0], NULL);
        if (!code) { app->writing = 0; code = result.code; }
      }
    }
  }
  return code;
}

static int write_all(int fd, const unsigned char *bytes, size_t length) {
  while (length) {
    ssize_t count = write(fd, bytes, length);
    if (count > 0) { bytes += count; length -= (size_t)count; }
    else if (count >= 0 || errno != EINTR) return 0;
  }
  return 1;
}

/* Self child uses only native byte I/O. Parent drains stdout and stderr
 * concurrently, without waiting for the child before reading either pipe. */
static int child(void) {
  const unsigned char errors[] = "child stderr\n";
  if (!write_all(2, errors, sizeof(errors) - 1)) return 90;
  unsigned char bytes[FILE_BYTES];
  for (;;) {
    ssize_t count = read(0, bytes, sizeof(bytes));
    if (!count) return 37;
    if (count < 0) { if (errno == EINTR) continue; return 91; }
    if (!write_all(1, bytes, (size_t)count)) return 92;
  }
}

#define SDK(call) do { code = (call); if (code) { failed = #call; goto cleanup; } } while (0)
#define REQUIRE(test) do { if (!(test)) { code = QAPLET_HOST_ERROR; failed = #test; goto cleanup; } } while (0)

static int run(const char *mode, const char *self, const char *launcher, const char *file) {
  application *app = calloc(1, sizeof(*app));
  if (!app) return 1;
  app->receive = app->transmit = -1;
  app->work = (qaplet_work_result)QAPLET_WORK_RESULT_INIT;
  app->process = (qaplet_process)QAPLET_PROCESS_INIT;
  app->child = (qaplet_process_status)QAPLET_PROCESS_STATUS_INIT;
  qaplet_code code = QAPLET_OK;
  qaplet_code destroy_code = QAPLET_BUSY;
  const char *failed = NULL;
  int complete = 0, destroyed = 0;
  int pending_demo = !strcmp(mode, "--cleanup-pending");
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_m2_diagnostic creation = QAPLET_M2_DIAGNOSTIC_INIT;
  app->gated = !strcmp(mode, "--cancel") || pending_demo;
  if (app->gated) {
    REQUIRE(!pthread_mutex_init(&app->gate_mutex, NULL));
    int error = pthread_cond_init(&app->gate_condition, NULL);
    if (error) (void)pthread_mutex_destroy(&app->gate_mutex);
    REQUIRE(!error);
    app->gate_initialized = 1;
  }
  REQUIRE(open_channel(app));
  SDK(qaplet_create_m2_with_diagnostic(&base, &config, &app->runtime, &creation, NULL));
  qaplet_m2_snapshot created = QAPLET_M2_SNAPSHOT_INIT;
  SDK(qaplet_get_m2_snapshot(app->runtime, &created, NULL));
  printf("{\"stage\":\"created\",\"capabilities\":%" PRIu64 ",\"helper_limit\":%" PRIu64 ",\"rollback\":%u}\n",
         created.capabilities, created.m2_limits.helper_threads, creation.creation_rollback);
  qaplet_host_notify hook = QAPLET_HOST_NOTIFY_INIT;
  hook.function = notify; hook.userdata = app;
  SDK(qaplet_set_host_notify(app->runtime, &hook, NULL));
  qaplet_work_handler handler = QAPLET_WORK_HANDLER_INIT;
  handler.name = "read-file-prefix"; handler.name_length = 16;
  handler.function = read_file; handler.userdata = app;
  handler.input_bytes = PATH_BYTES; handler.output_bytes = FILE_BYTES;
  SDK(qaplet_register_work_handler(app->runtime, &handler, NULL));
  /* Registration declarations are copied. Function code and app are retained
   * by the Host until native QUIESCENT and external producer join. */
  const char bootstrap[] =
    "globalThis.eventCount=0;globalThis.eventCopied=false;"
    "import('qaplet:events').then(m=>m.onEvent(e=>{"
    "eventCopied=e.type===23&&e.token===42n&&e.payload.length===3&&"
    "e.payload[0]===0&&e.payload[1]===255&&e.payload[2]===17;eventCount++}))";
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  SDK(qaplet_eval(app->runtime, bootstrap, sizeof(bootstrap) - 1, &progress, NULL));
  while (progress.phase == QAPLET_PHASE_PENDING)
    SDK(qaplet_pump(app->runtime, &progress, NULL));
  REQUIRE(strlen(file) + 1 <= PATH_BYTES);
  SDK(qaplet_work_submit(app->runtime, "read-file-prefix", 16, file, strlen(file) + 1, &app->task, NULL));
  if (app->gated) {
    REQUIRE(wait_entered(app));
    SDK(qaplet_work_cancel(app->runtime, &app->task, NULL));
    qaplet_work_status cancelled = QAPLET_WORK_STATUS_INIT;
    SDK(qaplet_get_work_status(app->runtime, &app->task, &cancelled, NULL));
    REQUIRE(cancelled.code == QAPLET_CANCELLED && cancelled.physical_pending == 1);
    printf("{\"stage\":\"cancel\",\"accepted\":true,\"code\":%u,\"physical_pending\":%" PRIu64 "}\n",
           cancelled.code, cancelled.physical_pending);
    if (pending_demo) {
      /* Logical CANCELLED is already copyable/releasable. The public native
       * status above still proves the worker is inside its borrowed call.
       * Release invalidates this token; SDK storage remains physically held. */
      app->work.buffer = app->file;
      app->work.capacity = sizeof(app->file);
      SDK(qaplet_work_copy_result(app->runtime, &app->task, &app->work, NULL));
      SDK(qaplet_work_release(app->runtime, &app->task, NULL));
      app->work_done = 1;
    } else release_gate(app); /* Worker can now observe cancellation. */
  }
  SDK(qaplet_keepalive_create(app->runtime, &app->keepalive, NULL));
  REQUIRE(!pthread_create(&app->producer, NULL, produce, app));
  app->producer_started = 1;

  qaplet_process_options options = QAPLET_PROCESS_OPTIONS_INIT;
  qaplet_string arguments[3]; size_t argc = 0;
  int direct = !strcmp(launcher, "-");
  const char *executable = direct ? self : launcher;
  options.path = (qaplet_string){executable, strlen(executable)};
  arguments[argc++] = options.path;
  if (!direct) arguments[argc++] = (qaplet_string){self, strlen(self)};
  arguments[argc++] = (qaplet_string){"--child", 7};
  options.argv = arguments; options.argc = argc;
  /* PIPE defaults, captured Runtime cwd/environment, no extra inheritance. */
  SDK(qaplet_process_spawn(app->runtime, &options, &app->process, &app->child, NULL));
  for (int i = 0; i < 3; ++i) {
    app->streams[i] = (qaplet_io_endpoint)QAPLET_IO_ENDPOINT_INIT;
    app->operations[i] = (qaplet_io_operation)QAPLET_IO_OPERATION_INIT;
    SDK(qaplet_process_get_stdio(app->runtime, &app->process, i, &app->streams[i], NULL));
  }
  for (int i = 0; i < 2; ++i) {
    SDK(qaplet_io_read(app->runtime, &app->streams[i + 1], FILE_BYTES, &app->operations[i + 1], NULL));
    app->reading[i] = 1;
  }
  REQUIRE(host_wait(app, 3000) == 1); /* Real Host loop receives producer hint. */
  REQUIRE(!pthread_join(app->producer, NULL));
  app->producer_joined = 1;
  REQUIRE(!atomic_load(&app->producer_error));

  double start = milliseconds(); REQUIRE(start >= 0);
  double until = start + 5000;
  for (;;) {
    /* The public SDK driver handles its own readiness/timers. The external
     * loop separately drains coalesced Host hints and inspects business state;
     * there is no second executor and no exported SDK fd to wait on. */
    REQUIRE(host_wait(app, 0) >= 0 && !atomic_load(&app->notify_error));
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    SDK(qaplet_run_once(app->runtime, 100, &drive, NULL));
    SDK(collect_work(app));
    SDK(input(app));
    SDK(collect_stream(app, 0));
    SDK(collect_stream(app, 1));
    SDK(qaplet_process_get_status(app->runtime, &app->process, &app->child, NULL));
    if (app->work_done && app->input_started && !app->writing && app->eof[0] && app->eof[1] &&
        app->child.reaped && app->child.delivered && !app->child.referenced) break;
    REQUIRE(milliseconds() >= 0 && milliseconds() < until);
  }
  SDK(snapshot(app, "active"));
  SDK(qaplet_process_release(app->runtime, &app->process, NULL));
  const char event_result[] = "eventCount===1&&eventCopied";
  SDK(qaplet_eval(app->runtime, event_result, sizeof(event_result) - 1, &progress, NULL));
  qaplet_result event = QAPLET_RESULT_INIT;
  SDK(qaplet_get_result(app->runtime, &event, NULL));
  REQUIRE(event.type == QAPLET_VALUE_BOOL && event.boolean);
  puts("{\"stage\":\"event\",\"consumed\":1,\"copied\":true}");
  printf("{\"stage\":\"work\",\"state\":%u,\"code\":%u,\"error_domain\":%u,\"error\":%d,\"bytes\":%zu,\"hex\":\"",
         app->work.state, app->work.code, app->work.error_domain, app->work.error, app->work.length);
  hex(app->file, app->work.length); puts("\"}");
  printf("{\"stage\":\"process\",\"exited\":%u,\"reaped\":%u,\"delivered\":%u,\"native_exit_code\":%u,"
         "\"native_signal\":%u,\"raw_wait_status\":%d,\"stdin_written\":%zu,\"stdout_eof\":true,\"stderr_eof\":true,\"stdout_hex\":\"",
         app->child.exited, app->child.reaped, app->child.delivered, app->child.native_exit_code,
         app->child.native_signal, app->child.raw_wait_status, app->written);
  hex(app->output[0], app->output_length[0]); printf("\",\"stderr_hex\":\"");
  hex(app->output[1], app->output_length[1]); puts("\"}");
  REQUIRE(app->child.exited && !app->child.code && app->child.native_exit_code == 37);
  code = app->work.code;

cleanup:
  /* Even failure closes the Runtime and retains every native borrow. Withdraw
   * the only future producer and join its in-flight SDK calls before destroy. */
  if (app->producer_started && !app->producer_joined) {
    if (!pthread_join(app->producer, NULL)) app->producer_joined = 1;
    else { code = QAPLET_HOST_ERROR; failed = "producer join"; }
  }
  int retain_gate = pending_demo && !failed;
  if (!retain_gate) release_gate(app); /* Failed setup must release its worker. */
  if (app->runtime) {
    qaplet_code closed = qaplet_close(app->runtime, NULL);
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    if (!closed) {
      (void)snapshot(app, "closed");
      /* This is an SDK active-wait budget, not a hard deadline for close,
       * arbitrary native calls, the complete function or process exit. */
      qaplet_code quiet = qaplet_quiesce(app->runtime, retain_gate ? 1 : 2000, &drive, NULL);
      complete = !quiet && drive.reason == QAPLET_DRIVE_QUIESCENT;
      if (complete) {
        (void)snapshot(app, "quiescent");
        if (app->producer_started == app->producer_joined) {
          destroy_code = qaplet_destroy(app->runtime, NULL);
          destroyed = destroy_code == QAPLET_OK;
        }
      }
    }
    if (!complete || !destroyed) code = code ? code : QAPLET_BUSY;
    /* Deliberately demonstrate that CLOSED plus a finite wait does not permit
     * destroy. A BUSY result leaves the Runtime and all borrows intact. */
    if (retain_gate && !complete && app->producer_started == app->producer_joined)
      destroy_code = qaplet_destroy(app->runtime, NULL);
    printf("{\"stage\":\"cleanup\",\"native_cleanup_complete\":%s,\"destroyed\":%s,\"producer_joined\":%s,"
           "\"destroy_code\":%u,\"wait_reason\":%u,\"retained_until_process_exit\":%s}\n",
           complete ? "true" : "false", destroyed ? "true" : "false",
           app->producer_started == app->producer_joined ? "true" : "false",
           destroy_code, drive.reason, destroyed ? "false" : "true");
  } else {
    fprintf(stderr, "application-m2: creation code=%u rollback=%u uncertain_handles=%" PRIu64 "\n",
            code, creation.creation_rollback, creation.native_uncertain_handles);
  }
  if (failed) fprintf(stderr, "application-m2: %s code=%u\n", failed, code);
  /* Incomplete native cleanup retains app, callbacks, buffers and Host channel
   * through process exit. CLOSED or a failed destroy never authorizes free. */
  if (destroyed || !app->runtime) {
    if (app->receive >= 0) (void)close(app->receive);
    if (app->transmit >= 0) (void)close(app->transmit);
    if (app->gate_initialized) {
      (void)pthread_cond_destroy(&app->gate_condition);
      (void)pthread_mutex_destroy(&app->gate_mutex);
    }
    free(app);
  }
  return code ? 1 : 0;
}

/* The library capability set is distinct from the created M2 instance. Query
 * actual finite defaults; do not duplicate the SDK's default table in a Host. */
static int metadata(void) {
  qaplet_abi abi = QAPLET_ABI_INIT;
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  qaplet_m2_diagnostic creation = QAPLET_M2_DIAGNOSTIC_INIT;
  qaplet_m2_snapshot snapshot = QAPLET_M2_SNAPSHOT_INIT;
  qaplet_runtime *runtime = NULL;
  int retained = 0;
  qaplet_code code = qaplet_query_abi(&abi);
  if (!code) code = qaplet_create_m2_with_diagnostic(&base, &config, &runtime, &creation, NULL);
  if (!code) code = qaplet_get_m2_snapshot(runtime, &snapshot, NULL);
  if (runtime) {
    retained = 1;
    qaplet_code cleanup = qaplet_close(runtime, NULL);
    qaplet_drive_result drive = QAPLET_DRIVE_RESULT_INIT;
    if (!cleanup) cleanup = qaplet_quiesce(runtime, 2000, &drive, NULL);
    if (!cleanup && drive.reason != QAPLET_DRIVE_QUIESCENT) cleanup = QAPLET_BUSY;
    if (!cleanup) {
      cleanup = qaplet_destroy(runtime, NULL);
      if (!cleanup) retained = 0;
    }
    if (!code) code = cleanup;
  }
  if (code) {
    fprintf(stderr, "application-m2 metadata: code=%u rollback=%u runtime_retained=%s\n",
            code, creation.creation_rollback, retained ? "true" : "false");
    return 1;
  }
  printf("{\"abi_version\":%u,\"capabilities\":%" PRIu64 ",\"instance_capabilities\":%" PRIu64 ",\"defaults\":{",
         abi.abi_version, abi.capabilities, snapshot.capabilities);
  const qaplet_limits *limits = &snapshot.limits;
#define FIELD(name, comma) printf("\"" #name "\":%" PRIu64 comma, limits->name)
  FIELD(engine_heap_bytes, ","); FIELD(engine_stack_bytes, ","); FIELD(native_storage_bytes, ",");
  FIELD(source_bytes, ","); FIELD(module_count, ","); FIELD(source_cache_bytes, ",");
  FIELD(execution_timeout_ms, ","); FIELD(execution_dispatches, ","); FIELD(event_queue_slots, ",");
  FIELD(event_payload_bytes, ","); FIELD(bridge_value_bytes, ","); FIELD(host_arguments, ",");
  FIELD(pump_jobs, ","); FIELD(pump_events, "");
#undef FIELD
  printf("},\"m2_defaults\":{");
  const qaplet_m2_limits *m2 = &snapshot.m2_limits;
#define FIELD(name, comma) printf("\"" #name "\":%" PRIu64 comma, m2->name)
  FIELD(worker_threads, ","); FIELD(helper_threads, ","); FIELD(worker_stack_bytes, ","); FIELD(helper_stack_bytes, ",");
  FIELD(work_handlers, ","); FIELD(handler_concurrency, ","); FIELD(timers, ","); FIELD(reactor_registrations, ",");
  FIELD(native_handles, ","); FIELD(processes, ","); FIELD(keepalive_tokens, ","); FIELD(operations, ",");
  FIELD(operation_data_bytes, ","); FIELD(work_input_bytes, ","); FIELD(work_output_bytes, ","); FIELD(io_block_bytes, ",");
  FIELD(process_arguments, ","); FIELD(process_data_bytes, ","); FIELD(pump_io_bytes, "");
#undef FIELD
  puts("}}");
  return 0;
}

static int finish(int status) {
  /* The fixed CRT encodes a POSIX wait status when main returns on Windows.
   * Preserve the raw OS contract, after SDK cleanup and Host output flush.
   * A pending-cleanup demonstration keeps every native borrow until this
   * process exits; this exit is not evidence that SDK cleanup completed. */
  if (fflush(NULL) && !status) status = 1;
  if (IsWindows()) ExitProcess(status);
  return status;
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--metadata")) return finish(metadata());
  if (argc == 2 && !strcmp(argv[1], "--child")) return finish(child());
  if (argc == 5 && (!strcmp(argv[1], "--normal") || !strcmp(argv[1], "--file-error") ||
                    !strcmp(argv[1], "--cancel") || !strcmp(argv[1], "--cleanup-pending")))
    return finish(run(argv[1], argv[2], argv[3], argv[4]));
  fputs("usage: application-m2 MODE SELF LAUNCHER FILE\n"
        "       MODE: --normal | --file-error | --cancel | --cleanup-pending\n"
        "       application-m2 --metadata\n"
        "       application-m2 --child\n", stderr);
  return finish(2);
}
