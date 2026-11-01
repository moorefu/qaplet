/* SPDX-License-Identifier: MIT
 * Independent M0 native contracts; not a Runtime or SDK implementation. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__aarch64__)
#define ARCH "aarch64"
#elif defined(__x86_64__)
#define ARCH "x86_64"
#else
#define ARCH "unknown"
#endif

static _Thread_local uintptr_t local_value;
static pthread_key_t key;
static pthread_barrier_t barrier;
static uintptr_t addresses[4];
static int failures;

static void result(const char *name, int passed, long actual, long expected) {
  printf("{\"case_id\":\"pages64k.%s\",\"status\":\"%s\",\"actual\":%ld,\"expected\":%ld}\n",
         name, passed ? "pass" : "fail", actual, expected);
  if (!passed) ++failures;
}

static void *worker(void *opaque) {
  uintptr_t n = (uintptr_t)opaque;
  int bad = 0;
  local_value = 0x8100 + n;
  addresses[n] = (uintptr_t)&local_value;
  if (pthread_setspecific(key, (void *)(n + 1))) bad = 1;
  for (int round = 0; round < 128; ++round) {
    int rc = pthread_barrier_wait(&barrier);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) bad = 1;
    if (local_value != 0x8100 + n || (uintptr_t)pthread_getspecific(key) != n + 1) bad = 1;
    local_value += 0x100;
    rc = pthread_barrier_wait(&barrier);
    if (rc && rc != PTHREAD_BARRIER_SERIAL_THREAD) bad = 1;
    if (local_value != 0x8200 + n) bad = 1;
    local_value -= 0x100;
  }
  return (void *)(uintptr_t)bad;
}

static void threads(void) {
  pthread_t ids[4];
  int bad = pthread_key_create(&key, 0) != 0 || pthread_barrier_init(&barrier, 0, 4) != 0;
  local_value = 0x12345;
  if (bad) { result("pthread-tls", 0, -1, 0); return; }
  for (uintptr_t i = 0; i < 4; ++i) {
    if (pthread_create(ids + i, 0, worker, (void *)i)) {
      /* Failed cohort would strand the barrier; the process timeout is the
         backstop, and this explicit failure exits without reporting success. */
      result("pthread-create", 0, -1, 0);
      fflush(stdout);
      _exit(1);
    }
  }
  for (int i = 0; i < 4; ++i) {
    void *status = (void *)1;
    if (pthread_join(ids[i], &status) || status) bad = 1;
    for (int j = 0; j < i; ++j) if (addresses[i] == addresses[j]) bad = 1;
    if (addresses[i] == (uintptr_t)&local_value) bad = 1;
  }
  if (local_value != 0x12345 || pthread_getspecific(key) != 0) bad = 1;
  if (pthread_key_delete(key) || pthread_barrier_destroy(&barrier)) bad = 1;
  result("pthread-tls", !bad, bad, 0);
}

static void memory(long page) {
  const size_t sizes[] = {1, 65535, 65536, 65537, 1048577};
  int bad = 0;
  for (unsigned i = 0; i < sizeof(sizes) / sizeof(*sizes); ++i) {
    unsigned char *p = malloc(sizes[i]);
    if (!p) { bad = 1; continue; }
    memset(p, 0xa7, sizes[i]);
    for (size_t n = 0; n < sizes[i]; ++n) if (p[n] != 0xa7) bad = 1;
    free(p);
  }
  result("allocation", !bad, bad, 0);
  unsigned char *p = mmap(0, page * 3, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  bad = p == MAP_FAILED;
  if (!bad) {
    for (long i = 0; i < page * 3; ++i) p[i] = (unsigned char)(i * 17 + 3);
    for (long i = 0; i < page * 3; ++i) if (p[i] != (unsigned char)(i * 17 + 3)) bad = 1;
    errno = 0;
    int rc = munmap(p + 1, page);
    result("unaligned-munmap-rejected", rc == -1 && errno == EINVAL, errno, EINVAL);
    if (mprotect(p + page, page, PROT_READ)) bad = 1;
    if (p[page] != 3 || p[page * 2 - 1] != (unsigned char)((page * 2 - 1) * 17 + 3)) bad = 1;
    if (munmap(p, page * 3)) bad = 1;
  }
  result("mmap-mprotect-munmap", !bad, bad, 0);
  errno = 0;
  p = mmap(0, 0, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  result("zero-map-rejected", p == MAP_FAILED && errno == EINVAL, errno, EINVAL);
  errno = 0;
  p = mmap((void *)(uintptr_t)(page * 16 + 1), page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  result("unaligned-map-rejected", p == MAP_FAILED && errno == EINVAL, errno, EINVAL);
}

static unsigned char pattern(size_t offset) { return (unsigned char)(offset * 29 + 7); }

static void resources(long page) {
  char name[] = "/tmp/qaplet-pages64k-XXXXXX";
  int fd = mkstemp(name), bad = fd < 0;
  unsigned char bytes[4096], returned[4096];
  for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = pattern(i);
  if (!bad) {
    bad = write(fd, bytes, sizeof(bytes)) != sizeof(bytes) || lseek(fd, 0, SEEK_SET) != 0 ||
          read(fd, returned, sizeof(returned)) != sizeof(returned) || memcmp(bytes, returned, sizeof(bytes));
    errno = 0;
    void *p = mmap(0, page, PROT_READ, MAP_PRIVATE, fd, 1);
    result("unaligned-file-map-rejected", p == MAP_FAILED && errno == EINVAL, errno, EINVAL);
    if (close(fd) || unlink(name)) bad = 1;
    errno = 0;
    if (access(name, F_OK) != -1 || errno != ENOENT) bad = 1;
  }
  result("filesystem-cleanup", !bad, bad, 0);
  FILE *f = fopen("/zip/pages64k.bin", "rb");
  bad = !f;
  const long expected_size = 131089;
  if (f) {
    if (fseek(f, 0, SEEK_END) || ftell(f) != expected_size) bad = 1;
    long offsets[] = {0, page - 1, page, page + 1, expected_size - 1};
    for (unsigned i = 0; i < sizeof(offsets) / sizeof(*offsets); ++i) {
      if (fseek(f, offsets[i], SEEK_SET) || fgetc(f) != pattern(offsets[i])) bad = 1;
    }
    if (fclose(f)) bad = 1;
  }
  result("zip-resource-pages", !bad, bad, 0);
  f = fopen("/zip/pages64k.bin", "rb");
  bad = !f;
  if (f) {
    if (fseek(f, expected_size, SEEK_SET) || fread(returned, 1, 1, f) != 0 || !feof(f)) bad = 1;
    clearerr(f);
    if (fseek(f, expected_size + page, SEEK_SET) || fread(returned, 1, 1, f) != 0 || !feof(f)) bad = 1;
    errno = 0;
    if (fseek(f, -1, SEEK_SET) != -1 || errno != EINVAL) bad = 1;
    if (fclose(f)) bad = 1;
  }
  result("zip-resource-bounds", !bad, bad, 0);
  errno = 0;
  f = fopen("/zip/absent-pages64k.bin", "rb");
  result("zip-missing-rejected", !f && errno == ENOENT, errno, ENOENT);
  if (f) fclose(f);
}

int main(int argc, char **argv) {
  long page = sysconf(_SC_PAGESIZE);
  if (argc == 2 && !strcmp(argv[1], "--facts")) {
    printf("{\"probe\":\"pages64k\",\"arch\":\"%s\",\"page_size\":%ld}\n", ARCH, page);
    return 0;
  }
  if (argc != 2 || strcmp(argv[1], "--suite")) return 2;
  result("environment", !strcmp(ARCH, "aarch64") && page == 65536, page, 65536);
  if (failures) return 1;
  threads();
  memory(page);
  resources(page);
  return failures ? 1 : 0;
}
