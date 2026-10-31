/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

int main(void) {
    volatile int values[] = {6, 7};
    int answer = values[0] * values[1];
    long page_size;
    const char *arch;
#if defined(__aarch64__) || defined(_M_ARM64)
    arch = "aarch64";
#elif defined(__x86_64__) || defined(_M_X64)
    arch = "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    arch = "x86";
#else
    arch = "unknown";
#endif
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    page_size = (long)info.dwPageSize;
#else
    page_size = sysconf(_SC_PAGESIZE);
#endif
    if (answer != 42 || page_size <= 0) return 1;
    printf("{\"answer\":%d,\"execution_arch\":\"%s\",\"page_size\":%ld}\n",
           answer, arch, page_size);
    return 0;
}
