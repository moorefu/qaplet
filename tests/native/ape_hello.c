/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <unistd.h>

int main(void) {
#if defined(__x86_64__)
  const char *arch = "x86_64";
#elif defined(__aarch64__)
  const char *arch = "aarch64";
#else
  const char *arch = "unknown";
#endif
  printf("{\"probe\":\"hello\",\"arch\":\"%s\",\"page_size\":%ld}\n",
         arch, sysconf(_SC_PAGESIZE));
  return 0;
}
