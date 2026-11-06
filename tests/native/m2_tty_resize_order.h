/* Controlled real-Console ordering fixture; enabled only by its build flag. */
#define _COSMO_SOURCE 1
#include <poll.h>
#include <time.h>
#include "libc/nt/console.h"
static int gate(const char *);
static int resize_order_delivered;
static bool32 resize_order_query(int64_t handle, struct NtConsoleScreenBufferInfo *info) {
  bool32 ok = GetConsoleScreenBufferInfo(handle, info);
  if (ok && info->dwSize.X == 90 && info->dwSize.Y == 31 && !resize_order_delivered) {
    resize_order_delivered = 1;
    if (!gate("resize-query")) return 0;
    struct timespec start, current;
    struct NtConsoleScreenBufferInfo latest = {0};
    clock_gettime(CLOCK_MONOTONIC, &start);
    do {
      if (GetConsoleScreenBufferInfo(handle, &latest) && latest.dwSize.X == 100 && latest.dwSize.Y == 33) return ok;
      poll(0, 0, 1);
      clock_gettime(CLOCK_MONOTONIC, &current);
    } while (current.tv_sec - start.tv_sec < 2);
    return 0;
  }
  return ok;
}
#define GetConsoleScreenBufferInfo resize_order_query
