/* Real OS probe of the pinned syscall thunk, not a product implementation. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include "libc/dce.h"
extern long sys_getdirentries(int,void *,size_t,long *);
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"fd probe line %d: %s errno=%d\n",__LINE__,#c,errno); return 1; } } while(0)
static int sweep(int directory,int keep,int *entries,int *highest,int *pages) {
  unsigned char buffer[512];long base=0;
  for (;;) {
    long n=sys_getdirentries(directory,buffer,sizeof(buffer),&base);
    if (n<0) return errno;
    if (!n) return 0;
    ++*pages;
    for (size_t offset=0;offset<(size_t)n;) {
      /* Darwin syscall 196: uint32 inode, uint16 reclen, u8 type, u8 namlen. */
      if ((size_t)n-offset<8) return EIO;
      uint16_t length;memcpy(&length,buffer+offset+4,2);
      unsigned namelen=buffer[offset+7];
      if (length<9 || length>(size_t)n-offset || namelen>=length-8 || buffer[offset+8+namelen]) return EIO;
      char *name=(char *)buffer+offset+8;
      if (namelen && name[0]>='0' && name[0]<='9') {
        unsigned fd=0;
        for(unsigned i=0;i<namelen;++i) { if (name[i]<'0' || name[i]>'9' || fd>(INT_MAX-(name[i]-'0'))/10u) return EIO;fd=fd*10+name[i]-'0'; }
        ++*entries;if ((int)fd>*highest) *highest=fd;
        if (fd>2 && fd!=directory && fd!=keep && close(fd) && errno!=EBADF) return errno;
      }
      offset+=length;
    }
  }
}
int main(void) {
  if (!IsXnu()) { fprintf(stderr,"fd probe requires native Darwin\n");return 2; }
  struct rlimit original,lowered,after;CHECK(!getrlimit(RLIMIT_NOFILE,&original));
  CHECK(original.rlim_cur>512);
  int directory=open("/dev/fd",O_RDONLY|O_DIRECTORY|O_CLOEXEC);CHECK(directory>=3);
  int sentinel=open("/dev/null",O_RDWR);CHECK(sentinel>=3);
  int high=fcntl(sentinel,F_DUPFD,original.rlim_cur>8192 ? 8192 : 512);CHECK(high>=512 && !fcntl(high,F_SETFD,0));
  int sentinels[180];for(unsigned i=0;i<180;++i) { sentinels[i]=dup(sentinel);CHECK(sentinels[i]>=0); }
  int report[2];CHECK(!pipe2(report,O_CLOEXEC));
  lowered=original;lowered.rlim_cur=256;CHECK(!setrlimit(RLIMIT_NOFILE,&lowered));
  CHECK(fcntl(high,F_GETFD)==0);
  pid_t pid=fork();CHECK(pid>=0);
  if (!pid) {
    struct { int error,entries,highest,pages,high_closed,low_closed; } facts={0};
    facts.error=sweep(directory,report[1],&facts.entries,&facts.highest,&facts.pages);
    errno=0;facts.high_closed=fcntl(high,F_GETFD)<0 && errno==EBADF;
    facts.low_closed=1;
    for(unsigned i=0;i<180;++i) { errno=0;if (fcntl(sentinels[i],F_GETFD)>=0 || errno!=EBADF) facts.low_closed=0; }
    ssize_t n=write(report[1],&facts,sizeof(facts));
    _exit(n==sizeof(facts) && !facts.error && facts.high_closed && facts.low_closed && facts.pages>1 ? 0 : 1);
  }
  CHECK(!close(report[1]));
  struct { int error,entries,highest,pages,high_closed,low_closed; } facts={0};
  CHECK(read(report[0],&facts,sizeof(facts))==sizeof(facts));int status;CHECK(waitpid(pid,&status,0)==pid);
  CHECK(!getrlimit(RLIMIT_NOFILE,&after));
  int parent_ok=after.rlim_cur==lowered.rlim_cur && after.rlim_max==original.rlim_max && fcntl(high,F_GETFD)==0;
  for(unsigned i=0;i<180;++i) { if (fcntl(sentinels[i],F_GETFD)<0) parent_ok=0;CHECK(!close(sentinels[i])); }
  CHECK(!setrlimit(RLIMIT_NOFILE,&original));
  CHECK(!close(high) && !close(sentinel) && !close(directory) && !close(report[0]));
  printf("{\"arch\":\"%s\",\"page_size\":%ld,\"hard\":%llu,\"soft\":%llu,\"lowered_soft\":256,\"high_fd\":%d,\"sweep_error\":%d,\"entries\":%d,\"highest\":%d,\"pages\":%d,\"high_closed\":%d,\"low_closed\":%d,\"parent_unchanged\":%d,\"wait_status\":%d}\n",IsAarch64() ? "aarch64" : "x86_64",sysconf(_SC_PAGESIZE),(unsigned long long)original.rlim_max,(unsigned long long)original.rlim_cur,high,facts.error,facts.entries,facts.highest,facts.pages,facts.high_closed,facts.low_closed,parent_ok,status);
  return !WIFEXITED(status) || WEXITSTATUS(status) || !parent_ok;
}
