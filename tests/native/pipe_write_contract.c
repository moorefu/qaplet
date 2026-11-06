/* SPDX-License-Identifier: MIT */
/* Exercise the product writer with real Linux signals and Darwin's syscall
 * availability/descriptor flags controlled at the OS boundary. Native Darwin
 * SDK contracts remain necessary; this fixture is not a native-host result. */
#define _GNU_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static int emulated_xnu,descriptor_flag,watched_fd=-1,partial_signal;
static volatile sig_atomic_t signals;
static void handler(int signal){if(signal==SIGPIPE)++signals;}
static int test_fcntl(int fd,int command,...){
  if(fd!=watched_fd){errno=EBADF;return -1;}
  if(command==74)return descriptor_flag;
  if(command==73){va_list args;va_start(args,command);descriptor_flag=va_arg(args,int);va_end(args);return 0;}
  errno=EINVAL;return -1;
}
static ssize_t test_write(int fd,const void *data,size_t size){
  if(emulated_xnu && fd==watched_fd && descriptor_flag){errno=EPIPE;return -1;}
  if(partial_signal && fd==watched_fd){
    ssize_t n=write(fd,data,partial_signal==1 ? 1 : size);if(n>0)raise(SIGPIPE);return n;
  }
  return write(fd,data,size);
}
static int test_sigtimedwait(const sigset_t *set,siginfo_t *info,const struct timespec *timeout){
  if(emulated_xnu){errno=ENOSYS;return -1;}return sigtimedwait(set,info,timeout);
}
#define fcntl test_fcntl
#define write test_write
#define sigtimedwait test_sigtimedwait
#include "pipe_write.h"
#undef fcntl
#undef write
#undef sigtimedwait
#define CHECK(c) do{if(!(c)){fprintf(stderr,"pipe writer line %d: %s errno=%d signals=%d\n",__LINE__,#c,errno,(int)signals);return 1;}}while(0)
static int check(int xnu,int blocked,int preset){
  int fds[2];CHECK(!pipe(fds) && !close(fds[0]));watched_fd=fds[1];emulated_xnu=xnu;descriptor_flag=preset;signals=0;
  sigset_t mask,before,after,pending;sigemptyset(&mask);sigaddset(&mask,SIGPIPE);
  CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&before));
  if(blocked)CHECK(!pthread_sigmask(SIG_BLOCK,&mask,NULL) && !raise(SIGPIPE));
  CHECK(qaplet_pipe_write(fds[1],"x",1)==-1 && errno==EPIPE);
  CHECK(!signals && descriptor_flag==preset);
  struct sigaction action;CHECK(!sigaction(SIGPIPE,NULL,&action) && action.sa_handler==handler);
  CHECK(!pthread_sigmask(SIG_SETMASK,NULL,&after));
  for(int s=1;s<NSIG;++s)CHECK(sigismember(&after,s)==(s==SIGPIPE && blocked ? 1 : sigismember(&before,s)));
  CHECK(!sigpending(&pending) && !!sigismember(&pending,SIGPIPE)==blocked);
  if(blocked){CHECK(!pthread_sigmask(SIG_UNBLOCK,&mask,NULL) && signals==1);}
  CHECK(!pthread_sigmask(SIG_SETMASK,&before,NULL) && !close(fds[1]));return 0;
}
static int positive_write_signal(void){
  int fds[2];CHECK(!pipe(fds));watched_fd=fds[1];emulated_xnu=0;signals=0;partial_signal=1;
  /* Linux may return an accepted prefix and generate SIGPIPE when the reader
   * closes during the write. Keep the real prefix; control that signal edge. */
  CHECK(qaplet_pipe_write(fds[1],"xy",2)==1);
  partial_signal=0;CHECK(!signals);char byte=0;CHECK(read(fds[0],&byte,1)==1 && byte=='x');
  CHECK(!close(fds[0]) && !close(fds[1]));return 0;
}
static int successful_write_host_signal(void){
  int fds[2];CHECK(!pipe(fds));watched_fd=fds[1];emulated_xnu=0;signals=0;partial_signal=2;
  /* A full write cannot generate pipe failure. A concurrent Host SIGPIPE must
   * retain its normal delivery, rather than being swallowed by this writer. */
  CHECK(qaplet_pipe_write(fds[1],"xy",2)==2);
  partial_signal=0;CHECK(signals==1);char bytes[2];CHECK(read(fds[0],bytes,2)==2 && !memcmp(bytes,"xy",2));
  CHECK(!close(fds[0]) && !close(fds[1]));return 0;
}
int main(void){
  CHECK(signal(SIGPIPE,handler)!=SIG_ERR);
  for(int xnu=0;xnu<2;++xnu)for(int blocked=0;blocked<2;++blocked)for(int preset=0;preset<2;++preset)CHECK(!check(xnu,blocked,preset));
  CHECK(!positive_write_signal());
  CHECK(!successful_write_host_signal());
  puts("pipe writer: broken pipe preserves Host handler, mask, pending signal and Darwin descriptor flags: pass");return 0;
}
