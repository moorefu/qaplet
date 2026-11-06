/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_PIPE_WRITE_H
#define QAPLET_PIPE_WRITE_H
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include "libc/dce.h"

/* Preserve the embedding Host's disposition, mask and existing SIGPIPE. */
static ssize_t qaplet_pipe_write(int fd,const void *data,size_t n) {
  if(IsXnu()){
    /* XNU has no sigtimedwait in the pinned libc. Its per-descriptor fcntl
     * controls suppress generation without changing any Host signal state.
     * Darwin fcntl.h: F_SETNOSIGPIPE=73, F_GETNOSIGPIPE=74. */
    int previous=fcntl(fd,74,0);if(previous<0)return -1;
    if(!previous && fcntl(fd,73,1)<0)return -1;
    ssize_t result=write(fd,data,n);int saved=errno;
    if(!previous && fcntl(fd,73,0)<0)return -1;
    errno=saved;return result;
  }
  sigset_t blocked,previous,pending;sigemptyset(&blocked);sigaddset(&blocked,SIGPIPE);
  int error=pthread_sigmask(SIG_BLOCK,&blocked,&previous);if(error){errno=error;return -1;}
  int was_pending=sigpending(&pending)==0 && sigismember(&pending,SIGPIPE);
  ssize_t result=write(fd,data,n);int saved=errno;
  /* A pipe write can accept a prefix and still generate SIGPIPE. Preserve its
   * positive byte count while consuming only a signal newly pending here. */
  if(!was_pending && ((result<0 && saved==EPIPE) || (result>0 && (size_t)result<n)) &&
     !sigpending(&pending) && sigismember(&pending,SIGPIPE)){
    struct timespec zero={0};while(sigtimedwait(&blocked,NULL,&zero)<0 && errno==EINTR){}
  }
  error=pthread_sigmask(SIG_SETMASK,&previous,NULL);errno=error ? error : saved;return error ? -1 : result;
}
#endif
