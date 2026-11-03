/* SPDX-License-Identifier: MIT */
/* Controlled SYSTEM-boundary fixture. Link supplied SDK with --wrap; never
 * includes private headers or injects by SDK allocation/call ordinal. */
#include "qaplet.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "controlled M2 contract line %d: %s\n", __LINE__, #c); return 1; } } while (0)
static int failure, mutexes, attributes, conditions, observe, observed, invalid_reentry;
static qaplet_runtime *watched;
int __real_pthread_mutex_init(pthread_mutex_t *, const pthread_mutexattr_t *);
int __real_pthread_mutex_destroy(pthread_mutex_t *);
int __real_pthread_condattr_init(pthread_condattr_t *);
int __real_pthread_condattr_destroy(pthread_condattr_t *);
int __real_pthread_condattr_setclock(pthread_condattr_t *, clockid_t);
int __real_pthread_cond_init(pthread_cond_t *, const pthread_condattr_t *);
int __real_pthread_cond_destroy(pthread_cond_t *);
int __real_clock_gettime(clockid_t, struct timespec *);
int __wrap_pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
  if (failure == 1) return ENOMEM;
  int error = __real_pthread_mutex_init(mutex, attr); if (!error) ++mutexes; return error;
}
int __wrap_pthread_mutex_destroy(pthread_mutex_t *mutex) {
  int error = __real_pthread_mutex_destroy(mutex); if (!error) --mutexes; return error;
}
int __wrap_pthread_condattr_init(pthread_condattr_t *attr) {
  if (failure == 2) return ENOMEM;
  int error = __real_pthread_condattr_init(attr); if (!error) ++attributes; return error;
}
int __wrap_pthread_condattr_destroy(pthread_condattr_t *attr) {
  int error = __real_pthread_condattr_destroy(attr); if (!error) --attributes; return error;
}
int __wrap_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock) {
  return failure == 3 ? EINVAL : __real_pthread_condattr_setclock(attr, clock);
}
int __wrap_pthread_cond_init(pthread_cond_t *condition, const pthread_condattr_t *attr) {
  if (failure == 4) return ENOMEM;
  int error = __real_pthread_cond_init(condition, attr); if (!error) ++conditions; return error;
}
int __wrap_pthread_cond_destroy(pthread_cond_t *condition) {
  int error = __real_pthread_cond_destroy(condition); if (!error) --conditions; return error;
}
int __wrap_clock_gettime(clockid_t clock, struct timespec *now) {
  if (observe && watched) {
    int mode = observe; observe = 0;
    qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
    qaplet_progress progress = QAPLET_PROGRESS_INIT;
    (void)mode;
    if (qaplet_quiesce(watched, 0, &result, NULL) != QAPLET_BUSY ||
        qaplet_run_once(watched, 0, &result, NULL) != QAPLET_BUSY ||
        qaplet_pump(watched, &progress, NULL) != QAPLET_BUSY ||
        qaplet_close(watched, NULL) != QAPLET_BUSY)
      invalid_reentry = 1;
    ++observed;
  }
  return __real_clock_gettime(clock, now);
}
int main(void) {
  qaplet_config base = QAPLET_CONFIG_INIT;
  qaplet_m2_config config = QAPLET_M2_CONFIG_INIT;
  for (int stage = 1; stage <= 4; ++stage) {
    qaplet_runtime *runtime = NULL;
    failure = stage;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_MEMORY_LIMIT && !runtime);
    CHECK(!mutexes && !attributes && !conditions);
    failure = 0;
    CHECK(qaplet_create_m2(&base, &config, &runtime, NULL) == QAPLET_OK);
    CHECK(qaplet_close(runtime, NULL) == QAPLET_OK && qaplet_destroy(runtime, NULL) == QAPLET_OK);
    CHECK(!mutexes && !attributes && !conditions);
  }
  CHECK(qaplet_create_m2(&base, &config, &watched, NULL) == QAPLET_OK);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_drive_result result = QAPLET_DRIVE_RESULT_INIT;
  const char source[] = "new Promise(()=>{})";
  CHECK(qaplet_eval(watched, source, sizeof(source)-1, &progress, NULL) == QAPLET_OK);
  observe = 1;
  CHECK(qaplet_run_once(watched, 0, &result, NULL) == QAPLET_OK && observed == 1 && !invalid_reentry);
  CHECK(qaplet_close(watched, NULL) == QAPLET_OK);
  observe = 2;
  CHECK(qaplet_quiesce(watched, 10, &result, NULL) == QAPLET_OK && observed == 2 && !invalid_reentry);
  CHECK(qaplet_destroy(watched, NULL) == QAPLET_OK);
  CHECK(!mutexes && !attributes && !conditions);
  puts("controlled M2: four system initialization refusals; resources balanced; PENDING/CLOSED whole-drive guard: pass");
  return 0;
}
