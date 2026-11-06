/* SPDX-License-Identifier: MIT */
/* Independent POSIX full-system guest controller; no Runtime/private layout. */
#define _COSMO_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t expired;
static void alarmed(int number) { (void)number; expired=1; }
static double milliseconds(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1000.0+t.tv_nsec/1e6; }
static const char *boolean(int value) { return value ? "true":"false"; }
static char *file_bytes(const char *path,size_t *length) {
  FILE *f=fopen(path,"rb");if(!f)return NULL;
  /* procfs reports size zero for live status/wchan; consume actual bytes. */
  size_t capacity=4096,used=0;char *text=malloc(capacity);if(!text){fclose(f);return NULL;}
  while(!feof(f)) {
    if(used==capacity-1) {
      if(capacity>=2097152){free(text);fclose(f);return NULL;}
      capacity*=2;char *next=realloc(text,capacity);if(!next){free(text);fclose(f);return NULL;}text=next;
    }
    size_t count=fread(text+used,1,capacity-used-1,f);used+=count;
    if(ferror(f)){free(text);fclose(f);return NULL;}
  }
  if(fclose(f)){free(text);return NULL;}text[used]=0;
  if(length)*length=used;return text;
}
static int put_file(const char *name,const char *value) {
  FILE *f=fopen(name,"wb");return f && fwrite(value,1,strlen(value),f)==strlen(value) && !fclose(f) ? 0:-1;
}
static int wait_child(pid_t child,int *status,unsigned seconds) {
  expired=0;struct sigaction action={0},prior={0};action.sa_handler=alarmed;sigemptyset(&action.sa_mask);
  if(sigaction(SIGALRM,&action,&prior))return -1;alarm(seconds);
  pid_t got;do {got=waitpid(child,status,0);}while(got<0 && errno==EINTR && !expired);
  alarm(0);sigaction(SIGALRM,&prior,NULL);
  if(got==child)return 0;
  kill(child,SIGKILL);while(waitpid(child,status,0)<0 && errno==EINTR){}
  return -1;
}
static int native_identity(pid_t pid,pid_t parent,const char *loader,const char *prefix) {
  char path[128],actual[4096];snprintf(path,sizeof(path),"/proc/%d/status",pid);
  char *status=file_bytes(path,NULL);if(!status)return 0;
  char *line=strstr(status,"\nPPid:");int measured=-1;int good=line && sscanf(line,"\nPPid:\t%d",&measured)==1 && measured==parent;
  char record_path[128];snprintf(record_path,sizeof(record_path),"%s-native-status.txt",prefix);put_file(record_path,status);free(status);
  snprintf(path,sizeof(path),"/proc/%d/exe",pid);ssize_t n=readlink(path,actual,sizeof(actual)-1);
  if(n<0)return 0;actual[n]=0;int live=kill(pid,0),error=live ? errno:0;
  good=good && !strcmp(actual,loader) && !live;
  snprintf(record_path,sizeof(record_path),"%s-native-identity.json",prefix);
  FILE *record=fopen(record_path,"wb");if(!record)return 0;
  fprintf(record,"{\"child_pid\":%d,\"expected_owner_pid\":%d,\"actual_parent_pid\":%d,\"native_exe\":\"%s\",\"expected_loader\":\"%s\",\"kill_zero_return\":%d,\"kill_zero_errno\":%d,\"identity_matched\":%s}\n",pid,parent,measured,actual,loader,live,error,boolean(good));
  if(fclose(record))return 0;return good;
}
static int send_native_signal(pid_t child,int number) {
  int result=kill(child,number),error=result ? errno:0;
  FILE *f=fopen("controller-signals.ndjson","ab");
  if(!f)return -1;
  int written=fprintf(f,"{\"sender_pid\":%d,\"target_pid\":%d,\"signal\":%d,\"return_value\":%d,\"errno\":%d,\"monotonic_ms\":%.6f}\n",getpid(),child,number,result,error,milliseconds());
  if(written<=0 || fclose(f))return -1;errno=error;return result;
}

static int signal_case(const char *cli,const char *loader,const char *helper,int number,const char *mode) {
  int queue=!strcmp(mode,"queue"),strong=!strcmp(mode,"strong"),help=!strcmp(mode,"help"),output=!strcmp(mode,"output");
  int budget=!strcmp(mode,"budget"),late=!strcmp(mode,"late");
  for(unsigned i=0;i<7;++i){const char *names[]={"controller-signals.ndjson","signal-fault.ndjson","signal-late.ndjson","late-arm","strong-child.json","strong-child-arm","queue-result.json"};unlink(names[i]);}
  char source[12000];
  if(strong)snprintf(source,sizeof(source),"import('qaplet:process').then(p=>{const a=['%s','%s'];p.spawn(a[0],{argv:a,stdio:['ignore','inherit','ignore']});for(;;){}})",loader,helper);
  else if(output)strcpy(source,"'X'.repeat(200000)");
  else snprintf(source,sizeof(source),"Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(async([s,t])=>{await s.stdout.write(new Uint8Array([82,69,65,68,89,10])).promise;%s})",
                !strcmp(mode,"execute") ? "for(;;){}":"await new Promise(r=>t.setTimeout(r,600000));await s.stdout.write(new Uint8Array([65,70,84,69,82])).promise;");
  if(strong && prctl(PR_SET_CHILD_SUBREAPER,1,0,0,0))return 1;
  int pipefd[2];if(pipe(pipefd))return 1;
  /* Linux UAPI F_SETPIPE_SZ/F_GETPIPE_SZ. A 64K kernel's default pipe can hold
   * the whole 200000-byte result; explicitly retain a small, measured pipe. */
  int pipe_capacity=0;
  if(output || help) {
    if(fcntl(pipefd[0],1031,65536)<0)return 1;
    pipe_capacity=fcntl(pipefd[0],1032);
    if(pipe_capacity<=0 || pipe_capacity>=200000)return 1;
  }
  int error=open("cli.stderr",O_WRONLY|O_CREAT|O_TRUNC,0600),input=open("/dev/null",O_RDONLY);
  if(error<0 || input<0)return 1;
  if(help) {
    int old=fcntl(pipefd[1],F_GETFL);fcntl(pipefd[1],F_SETFL,old|O_NONBLOCK);char fill[4096];memset(fill,'F',sizeof(fill));
    while(write(pipefd[1],fill,sizeof(fill))>0){}fcntl(pipefd[1],F_SETFL,old);
  }
  pid_t child=fork();if(child<0)return 1;
  if(!child) {
    unsetenv("QAPLET_SIGNAL_THREAD_FAULT");unsetenv("QAPLET_SIGNAL_SPLIT_WAIT");unsetenv("QAPLET_SIGNAL_LATE");
    if(budget){setenv("QAPLET_SIGNAL_THREAD_FAULT","1",1);setenv("QAPLET_SIGNAL_SPLIT_WAIT","1",1);}
    if(late)setenv("QAPLET_SIGNAL_LATE","1",1);
    dup2(input,0);dup2(pipefd[1],1);dup2(error,2);close(input);close(error);close(pipefd[0]);close(pipefd[1]);
    char *normal[]={(char *)loader,(char *)cli,"--timeout-ms","600000","--max-dispatches","100000000","eval","--source",source,NULL};
    char *control[]={(char *)loader,(char *)(queue ? helper:cli),(char *)(queue ? "--queue-stop":"--help"),NULL};
    execv(loader,queue || help ? control:normal);_exit(127);
  }
  close(input);close(error);close(pipefd[1]);fcntl(pipefd[0],F_SETFL,fcntl(pipefd[0],F_GETFL)|O_NONBLOCK);
  char bytes[300000],wait_channel[256]={0};size_t used=0,first_read=0;int ready=0,identity=0;pid_t direct=-1;double until=milliseconds()+20000;
  while(milliseconds()<until && !ready) {
    if(strong && direct<0) {
      char *record=file_bytes("strong-child.json",NULL);
      if(record){int measured=-1;if(sscanf(record,"{\"native_pid\":%d",&measured)==1 && measured>1){
        direct=measured;identity=native_identity(direct,child,loader,"strong");
        if(identity)put_file("strong-child-arm","Exact live native PID/PPid/exe verified before READY");
      }free(record);}
    }
    if(help || (output && used)) {
      char path[128];snprintf(path,sizeof(path),"/proc/%d/wchan",child);char *wait=file_bytes(path,NULL);
      if(wait){ready=strstr(wait,"poll") || strstr(wait,"pipe_write");if(ready)snprintf(wait_channel,sizeof(wait_channel),"%s",wait);free(wait);}
    } else {
      struct pollfd pollfd={pipefd[0],POLLIN,0};int n=poll(&pollfd,1,10);
      if(n>0 && pollfd.revents&POLLIN && used<sizeof(bytes)-1){ssize_t got=read(pipefd[0],bytes+used,output ? 4096:sizeof(bytes)-1-used);if(got>0){used+=got;first_read=used;bytes[used]=0;ready=output ? 0:strstr(bytes,"READY\n")!=NULL;}}
    }
    if(kill(child,0))break;
  }
  int sent=-1,signal_error=0,status=0,held=0,signal_count=0;double before=milliseconds();
  if(ready) {
    if(late)put_file("late-arm","Actual completed stdout READY accepted");
    else {sent=send_native_signal(child,number);signal_error=sent ? errno:0;++signal_count;}
  }
  if(ready && (budget || late)) {
    double until=milliseconds()+2000;
    do {
      char *record=file_bytes(late ? "signal-late.ndjson":"signal-fault.ndjson",NULL);
      if(record){held=strstr(record,late ? "late-hint-held":"thread-exit-held")!=NULL;free(record);}
      if(held)break;
      struct timespec tick={0,1000000};nanosleep(&tick,NULL);
    } while(milliseconds()<until);
    if(held)for(unsigned i=0;i<(late ? 1u:5u);++i) {
      int next=send_native_signal(child,late || i%2 ? SIGTERM:SIGINT);++signal_count;
      if(next){sent=next;signal_error=errno;}else if(late)sent=0;
      if(!late){struct timespec pace={0,100000000};while(nanosleep(&pace,&pace) && errno==EINTR){}}
    }
  }
  int timeout=wait_child(child,&status,12)!=0;
  for(;;){ssize_t got=read(pipefd[0],bytes+used,sizeof(bytes)-1-used);if(got<=0)break;used+=got;if(used>=sizeof(bytes)-1)break;}
  close(pipefd[0]);bytes[used]=0;FILE *raw=fopen("cli.stdout","wb");if(raw){fwrite(bytes,1,used,raw);fclose(raw);}
  int physical=0,reaped=0;
  if(strong && direct>1) {
    int ds=0;pid_t got=waitpid(direct,&ds,WNOHANG);reaped=got==direct;
    char path[128];snprintf(path,sizeof(path),"/proc/%d",direct);physical=access(path,F_OK)!=0;
    if(!physical){kill(direct,SIGKILL);while(waitpid(direct,&ds,0)<0 && errno==EINTR){}}
  }
  char *errors=file_bytes("cli.stderr",NULL),*queue_result=queue ? file_bytes("queue-result.json",NULL):NULL;
  int code=WIFEXITED(status) ? WEXITSTATUS(status):-1;
  int good=ready && !sent && !timeout && code==1 && errors && strstr(errors,"EXECUTION_STOPPED") &&
           !strstr(errors,"cleanup incomplete") && !strstr(bytes,"AFTER") && (!strong || (identity && physical));
  if(queue)good=good && queue_result && strstr(queue_result,"\"sdk_destroyed\":true") && strstr(queue_result,"\"user_marks\":0");
  double elapsed=milliseconds()-before,active_wait_ms=0;unsigned long_waits=0;
  if(budget) {
    char *record=file_bytes("signal-fault.ndjson",NULL),*cursor=record;
    while(cursor && (cursor=strstr(cursor,"\"operation\":\"active-cond-wait\""))) {
      char *duration=strstr(cursor,"\"duration_ns\":");unsigned long long ns=0;
      if(duration && sscanf(duration,"\"duration_ns\":%llu",&ns)==1){active_wait_ms+=ns/1e6;if(ns>500000000)++long_waits;}++cursor;
    }
    free(record);
    good=ready && held && !sent && signal_count==6 && !timeout && code==1 && errors && strstr(errors,"native cleanup incomplete") &&
         !strstr(bytes,"AFTER") && elapsed>=4500 && elapsed<6500 && active_wait_ms>=4500 && active_wait_ms<5500 && long_waits>=2;
  }
  if(late) {
    char *record=file_bytes("signal-late.ndjson",NULL);
    good=good && held && signal_count==1 && record && strstr(record,"\"destroy_code\":0") &&
         strstr(record,"\"original_target_identity_preserved\":true") && strstr(record,"\"replacement_identity_reused\":false") &&
         strstr(record,"\"late_entry_completed\":true") && strstr(record,"\"replacement_objects_untouched\":true") && strstr(record,"\"future_entry_delivered\":true");
    free(record);
  }
  printf("{\"case_id\":\"cli.signal.%s.%s\",\"status\":\"%s\",\"actual\":{\"ready\":%s,\"signal\":%d,\"signal_return\":%d,\"signal_errno\":%d,\"exit_code\":%d,\"supervisor_timeout\":%s,\"elapsed_after_signal_ms\":%.6f,\"controller_child_reaped\":true,\"strong_native_identity_before_ready\":%s,\"strong_child_physically_exited\":%s,\"controller_reaped_adopted_child\":%s,\"sdk_all_native_cleanup_directly_observed\":%s,\"owner_pid\":%d,\"direct_pid\":%d,\"pipe_capacity_bytes\":%d,\"first_read_bytes\":%zu,\"sender_observed_wchan\":\"%s\",\"signals_attempted\":%d,\"native_entry_or_exit_held\":%s,\"cumulative_active_wait_ms\":%.6f,\"active_waits_over_500ms\":%u}}\n",
         number==SIGINT ? "SIGINT":"SIGTERM",mode,good ? "pass":"fail",boolean(ready),number,sent,sent ? signal_error:0,code,boolean(timeout),milliseconds()-before,
         boolean(identity),boolean(physical),boolean(reaped),budget ? "false":(queue || late) ? "true":"null",child,direct,pipe_capacity,first_read,wait_channel,
         signal_count,boolean(held),active_wait_ms,long_waits);
  free(errors);free(queue_result);return !good;
}

typedef struct {int fd,error,writer;const char *path;const unsigned char *data;size_t length,bytes;} transfer;
static void *pipe_transfer(void *opaque) {
  transfer *item=opaque;int output=-1;
  if(!item->writer) { output=open(item->path,O_WRONLY|O_CREAT|O_TRUNC,0600);if(output<0)item->error=errno; }
  unsigned char block[4096];
  while(!item->error) {
    const unsigned char *bytes=item->writer ? item->data+item->bytes:NULL;size_t size=item->writer ? item->length-item->bytes:0;
    if(!item->writer) {
      ssize_t n=read(item->fd,block,sizeof(block));
      if(n<0 && errno==EINTR)continue;if(n<0){item->error=errno;break;}if(!n)break;
      bytes=block;size=n;
    } else if(!size)break;
    size_t offset=0;
    while(offset<size) {
      ssize_t n=write(item->writer ? item->fd:output,bytes+offset,size-offset);
      if(n<0 && errno==EINTR)continue;
      if(n<=0){item->error=n<0 ? errno:EIO;break;}offset+=n;
    }
    item->bytes+=offset;
  }
  if(output>=0 && close(output) && !item->error)item->error=errno;
  if(close(item->fd) && !item->error)item->error=errno;return NULL;
}

static int binary_pipe_case(const char *cli,const char *loader,int large) {
  static const char source[]="Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(([s,t])=>{async function put(out,bytes){for(;;){try{return await out.write(bytes).promise}catch(e){if(e.code!==24||e.bytesWritten)throw e;await new Promise(resolve=>t.setImmediate(resolve))}}}(async()=>{for(;;){let r=await s.stdin.read(65536).promise;if(r.eof)break;await Promise.all([put(s.stdout,r.bytes),put(s.stderr,r.bytes)]);r.bytes.fill(7)}})();return 'ENTRY'})";
  unsigned char data[65792];for(size_t i=0;i<sizeof(data);++i)data[i]=i%256;
  int pairs[3][2],owned[3],before[3],after[3];
  for(int i=0;i<3;++i)if(pipe(pairs[i]))return 1;
  owned[0]=pairs[0][0];owned[1]=pairs[1][1];owned[2]=pairs[2][1];
  for(int i=0;i<3;++i){before[i]=fcntl(owned[i],F_GETFL);if(before[i]<0)return 1;}
  pid_t child=fork();if(child<0)return 1;
  if(!child) {
    if(setsid()<0)_exit(126);
    for(int i=0;i<3;++i)if(dup2(owned[i],i)!=i)_exit(126);
    for(int i=0;i<3;++i)for(int j=0;j<2;++j)close(pairs[i][j]);
    char *argv[]={(char *)loader,(char *)cli,"eval","--source",large ? "'X'.repeat(200000)":(char *)source,NULL};execv(loader,argv);_exit(127);
  }
  transfer transfers[3]={{pairs[0][1],0,1,NULL,data,large ? 0:sizeof(data),0},{pairs[1][0],0,0,"cli.stdout",NULL,0,0},{pairs[2][0],0,0,"cli.stderr",NULL,0,0}};
  pthread_t threads[3];int started[3]={0},joined=1;
  for(int i=0;i<3;++i){int error=pthread_create(&threads[i],NULL,pipe_transfer,&transfers[i]);if(error){transfers[i].error=error;close(transfers[i].fd);}else started[i]=1;}
  int status=0,timeout=wait_child(child,&status,12)!=0,flags=1;
  if(timeout)kill(-child,SIGKILL);
  for(int i=0;i<3;++i){after[i]=fcntl(owned[i],F_GETFL);flags=flags && after[i]==before[i];close(owned[i]);}
  for(int i=0;i<3;++i){int error=started[i] ? pthread_join(threads[i],NULL):-1;if(error || transfers[i].error)joined=0;}
  size_t outlen=0,errlen=0;char *out=file_bytes("cli.stdout",&outlen),*err=file_bytes("cli.stderr",&errlen);
  int code=WIFEXITED(status) ? WEXITSTATUS(status):-1;
  int bytes_match=0;
  if(out && err) {
    if(large) { bytes_match=outlen==200001 && !errlen && out[200000]=='\n';for(size_t i=0;bytes_match && i<200000;++i)bytes_match=out[i]=='X'; }
    else bytes_match=outlen==sizeof(data)+6 && errlen==sizeof(data) && !memcmp(out,data,sizeof(data)) && !memcmp(out+sizeof(data),"ENTRY\n",6) && !memcmp(err,data,sizeof(data));
  }
  int good=!timeout && code==0 && flags && joined && bytes_match;
  printf("{\"case_id\":\"cli.route.%s\",\"status\":\"%s\",\"actual\":{\"exit_code\":%d,\"supervisor_timeout\":%s,\"stdout_bytes\":%zu,\"stderr_bytes\":%zu,\"full_flags_restored\":%s,\"producer_and_readers_joined\":%s,\"before_flags\":[%d,%d,%d],\"restored_flags\":[%d,%d,%d],\"input_bytes\":%zu}}\n",large ? "large-result-restored":"pipe-binary-background",good ? "pass":"fail",code,boolean(timeout),outlen,errlen,boolean(flags),boolean(joined),before[0],before[1],before[2],after[0],after[1],after[2],large ? (size_t)0:sizeof(data));
  free(out);free(err);return !good;
}

static int file_route(const char *mode,const char *cli,const char *loader,const char *helper) {
  int binary=!strcmp(mode,"file-binary-background"),entry=!strcmp(mode,"file-entry-copy"),dual=!strcmp(mode,"process-dual-channel");
  int broken=!strcmp(mode,"final-output-broken"),invalid=!strcmp(mode,"final-output-invalid"),ordinary=!strcmp(mode,"ordinary-process-error");
  int allocation=!strcmp(mode,"background-allocation"),results=!strcmp(mode,"sdk-results");
  if(!binary && !entry && !dual && !broken && !invalid && !ordinary && !allocation && !results)return 64;
  char source[16000];
  static const char put[]="async function put(out,bytes){for(;;){try{return await out.write(bytes).promise}catch(e){if(e.code!==24||e.bytesWritten)throw e;await new Promise(resolve=>t.setImmediate(resolve))}}}";
  if(binary)snprintf(source,sizeof(source),"Promise.all([import('qaplet:stdio'),import('qaplet:timers')]).then(([s,t])=>{%s(async()=>{for(;;){let r=await s.stdin.read(65536).promise;if(r.eof)break;await Promise.all([put(s.stdout,r.bytes),put(s.stderr,r.bytes)]);r.bytes.fill(7)}})();return 'ENTRY'})",put);
  else if(entry)strcpy(source,"import('qaplet:stdio').then(s=>{s.stdout.write(new Uint8Array([66,0,90]));return 'entry\\0中'})");
  else if(dual)snprintf(source,sizeof(source),"Promise.all([import('qaplet:process'),import('qaplet:stdio'),import('qaplet:timers')]).then(([{spawn},s,t])=>{%sconst a=['%s','%s','--m2-cli-emit'];const p=spawn(a[0],{argv:a});p.stdin.finish();async function drain(input,output){for(;;){const r=await input.read(4096).promise;if(r.eof)break;await put(output,r.bytes)}}Promise.all([drain(p.stdout,s.stdout),drain(p.stderr,s.stderr),p.exited]).then(v=>{if(v[2].exitCode!==37)throw Error('child exit')});return 'ENTRY'})",put,loader,helper);
  else if(ordinary)strcpy(source,"import('qaplet:process').then(({spawn})=>{try{spawn('qaplet-missing-explicit-path',{stdio:['ignore','ignore','ignore']})}catch(e){if(e.code===27&&e.errorDomain&&e.error)return 42;throw e}throw Error('missing refusal')})");
  else if(allocation)strcpy(source,"import('qaplet:timers').then(t=>{t.setImmediate(()=>{try{let a=[];for(;;)a.push(new Uint8Array(1048576))}catch(e){}});return 'ENTRY'})");
  else strcpy(source,"'saved中🙂'");
  unsigned char data[131072];for(size_t i=0;i<sizeof(data);++i)data[i]=i%256;
  FILE *input=fopen("stdin.bin","wb");if(!input)return 1;
  if(binary && fwrite(data,1,65792,input)!=65792){fclose(input);return 1;}if(fclose(input))return 1;
  int fd[3]={open("stdin.bin",O_RDONLY),open("cli.stdout",O_WRONLY|O_CREAT|O_TRUNC,0600),open("cli.stderr",O_WRONLY|O_CREAT|O_TRUNC,0600)};
  int fault[2]={-1,-1};if(broken){if(pipe(fault))return 1;close(fault[0]);fault[0]=-1;}
  for(int i=0;i<3;++i)if(fd[i]<0)return 1;
  int before[3],after[3];for(int i=0;i<3;++i){before[i]=fcntl(i==1 && broken ? fault[1]:fd[i],F_GETFL);if(before[i]<0)return 1;}
  pid_t child=fork();if(child<0)return 1;
  if(!child) {
    if(setsid()<0)_exit(126);
    for(int i=0;i<3;++i)if(dup2(i==1 && broken ? fault[1]:fd[i],i)!=i)_exit(126);
    for(int i=0;i<3;++i)close(fd[i]);if(broken)close(fault[1]);if(invalid)close(1);
    char *argv[]={(char *)loader,(char *)(results ? helper:cli),"eval","--source",source,NULL};
    if(results){argv[2]="--m2-cli-results";argv[3]=NULL;}
    execv(loader,argv);_exit(127);
  }
  int status=0,timeout=wait_child(child,&status,12)!=0,flags=1;
  if(timeout)kill(-child,SIGKILL);
  for(int i=0;i<3;++i){after[i]=fcntl(i==1 && broken ? fault[1]:fd[i],F_GETFL);flags=flags && after[i]==before[i];close(fd[i]);}
  if(broken)close(fault[1]);
  size_t outlen=0,errlen=0;char *out=file_bytes("cli.stdout",&outlen),*err=file_bytes("cli.stderr",&errlen);
  int code=WIFEXITED(status) ? WEXITSTATUS(status):-1,expected=(broken || invalid || allocation) ? 1:0,bytes_match=0;
  if(out && err) {
    if(binary || dual){size_t n=binary ? 65792:131072;bytes_match=outlen==n+6 && errlen==n && !memcmp(out,data,n) && !memcmp(err,data,n) && !memcmp(out+n,"ENTRY\n",6);}
    else if(entry){static const char expected_bytes[]="B\0Zentry\0中\n";bytes_match=outlen==sizeof(expected_bytes)-1 && !memcmp(out,expected_bytes,sizeof(expected_bytes)-1) && !errlen;}
    else if(ordinary)bytes_match=outlen==3 && !memcmp(out,"42\n",3) && !errlen;
    else if(allocation || broken || invalid)bytes_match=!outlen && strstr(err,allocation ? "qaplet: code=11 MEMORY_LIMIT":"qaplet: code=30 IO_ERROR")!=NULL;
    else if(results)bytes_match=outlen>0 && strstr(out,"pass\n") && !errlen;
  }
  int good=!timeout && code==expected && flags && bytes_match && err && !strstr(err,"cleanup incomplete") && !strstr(err,"standard stream restoration incomplete");
  printf("{\"case_id\":\"cli.route.%s\",\"status\":\"%s\",\"actual\":{\"exit_code\":%d,\"supervisor_timeout\":%s,\"stdout_bytes\":%zu,\"stderr_bytes\":%zu,\"full_flags_restored\":%s,\"before_flags\":[%d,%d,%d],\"restored_flags\":[%d,%d,%d],\"actual_binary_bytes_matched\":%s}}\n",mode,good ? "pass":"fail",code,boolean(timeout),outlen,errlen,boolean(flags),before[0],before[1],before[2],after[0],after[1],after[2],boolean(bytes_match));
  free(out);free(err);return !good;
}

static int terminal_record(const char *stage,int fd,const struct termios *mode,int flags) {
  FILE *f=fopen("cli-mixed-terminal.ndjson","ab");if(!f)return -1;
  fprintf(f,"{\"stage\":\"%s\",\"iflag\":%u,\"oflag\":%u,\"cflag\":%u,\"lflag\":%u,\"ispeed\":%u,\"ospeed\":%u,\"flags\":%d,\"cc\":[",stage,
          (unsigned)mode->c_iflag,(unsigned)mode->c_oflag,(unsigned)mode->c_cflag,(unsigned)mode->c_lflag,(unsigned)cfgetispeed(mode),(unsigned)cfgetospeed(mode),flags);
  for(unsigned i=0;i<sizeof(mode->c_cc);++i)fprintf(f,"%s%u",i ? ",":"",(unsigned char)mode->c_cc[i]);
  fputs("]}\n",f);return fclose(f);
}
static int terminal_equal(const struct termios *a,const struct termios *b,int fa,int fb) {
  return fa==fb && a->c_iflag==b->c_iflag && a->c_oflag==b->c_oflag && a->c_cflag==b->c_cflag && a->c_lflag==b->c_lflag &&
         cfgetispeed(a)==cfgetispeed(b) && cfgetospeed(a)==cfgetospeed(b) && !memcmp(a->c_cc,b->c_cc,sizeof(a->c_cc));
}
static void *terminal_reader(void *opaque) {
  transfer *item=opaque;int output=open("cli.stdout",O_WRONLY|O_CREAT|O_TRUNC,0600);
  if(output<0)item->error=errno;
  char bytes[4096];
  while(!item->error) {
    ssize_t n=read(item->fd,bytes,sizeof(bytes));if(n<0 && errno==EINTR)continue;
    if(!n || (n<0 && errno==EIO))break;if(n<0){item->error=errno;break;}
    size_t at=0;while(at<(size_t)n){ssize_t written=write(output,bytes+at,n-at);if(written<0 && errno==EINTR)continue;if(written<=0){item->error=written<0 ? errno:EIO;break;}at+=written;}
    item->bytes+=at;
  }
  if(output>=0 && close(output) && !item->error)item->error=errno;
  if(close(item->fd) && !item->error)item->error=errno;return NULL;
}
static int mixed_signal_case(const char *cli,const char *loader,const char *helper,int number) {
  unlink("mixed-ready");unlink("direct-child.json");unlink("signal-native.ndjson");unlink("cli-mixed-terminal.ndjson");unlink("controller-signals.ndjson");
  int master=posix_openpt(O_RDWR|O_NOCTTY);if(master<0 || grantpt(master) || unlockpt(master))return 1;
  int slave=open(ptsname(master),O_RDWR|O_NOCTTY);if(slave<0)return 1;
  struct termios before={0},modified={0},restored={0};
  int before_flags=fcntl(slave,F_GETFL);if(tcgetattr(slave,&before) || before_flags<0 || terminal_record("before",slave,&before,before_flags))return 1;
  char source[16000];snprintf(source,sizeof(source),
      "Promise.all([import('qaplet:process'),import('qaplet:stdio'),import('qaplet:timers')]).then(async([p,s,t])=>{const a=['%s','%s','--held-child'];const child=p.spawn(a[0],{argv:a});const first=await child.stdout.read(1).promise;if(first.bytes[0]!==82)throw Error('child gate');t.setInterval(()=>{},600000);await s.stdin.releaseTerminal();await Promise.all([s.stdin.closed,s.stdout.closed,s.stderr.closed]);const input=s.acquireTerminal(0,{raw:true});input.joinTerminal(1,{vt:true});input.joinTerminal(2);try{await input.read(1).promise}finally{const b=[...a];b[b.length-1]='--forbidden';p.spawn(b[0],{argv:b})}})",loader,helper);
  pid_t child=fork();if(child<0)return 1;
  if(!child) {
    if(setsid()<0)_exit(126);
    for(int i=0;i<3;++i)if(dup2(slave,i)!=i)_exit(126);close(slave);close(master);
    char *argv[]={(char *)loader,(char *)cli,"--timeout-ms","600000","eval","--source",source,NULL};execv(loader,argv);_exit(127);
  }
  transfer reader={master,0,0,"cli.stdout",NULL,0,0};pthread_t thread;
  if(pthread_create(&thread,NULL,terminal_reader,&reader)){kill(child,SIGKILL);waitpid(child,NULL,0);close(slave);close(master);return 1;}
  double until=milliseconds()+20000;int ready=0;
  do {if(!access("mixed-ready",F_OK)){ready=1;break;}struct timespec tick={0,1000000};nanosleep(&tick,NULL);}while(milliseconds()<until);
  int modified_flags=fcntl(slave,F_GETFL),mode_read=tcgetattr(slave,&modified);
  if(!mode_read)terminal_record("modified",slave,&modified,modified_flags);
  char *direct_text=file_bytes("direct-child.json",NULL);int direct=-1;
  if(direct_text)sscanf(direct_text,"{\"pid\":%d",&direct);free(direct_text);
  int identity=ready && direct>1 && native_identity(direct,child,loader,"mixed");
  double start=milliseconds();int sent=ready && identity ? send_native_signal(child,number):-1,status=0;
  int timeout=wait_child(child,&status,12)!=0;if(timeout)kill(-child,SIGKILL);
  int restored_flags=fcntl(slave,F_GETFL),restore_read=tcgetattr(slave,&restored);
  if(!restore_read)terminal_record("restored",slave,&restored,restored_flags);
  int modes=!mode_read && !restore_read && terminal_equal(&before,&restored,before_flags,restored_flags) && !terminal_equal(&before,&modified,before_flags,modified_flags);
  close(slave);int joined=!pthread_join(thread,NULL) && !reader.error;
  char *native=file_bytes("signal-native.ndjson",NULL),*raw=file_bytes("cli.stdout",NULL);
  int proof=native && strstr(native,"\"operation\":\"mixed-tty-read-accepted\"") && strstr(native,"\"operation\":\"native-child-reaped\"") &&
            strstr(native,"\"signal\":9") && strstr(native,"\"operation\":\"owner-close\",\"code\":0,\"termination_reason\":23") &&
            strstr(native,"\"operation\":\"native-quiesce\",\"code\":0,\"reason\":6") && strstr(native,"\"operation\":\"runtime-destroy\",\"code\":0");
  int code=WIFEXITED(status) ? WEXITSTATUS(status):-1;
  int good=ready && identity && !sent && !timeout && code==1 && modes && joined && proof && raw && strstr(raw,"EXECUTION_STOPPED") &&
           !strstr(raw,"cleanup incomplete") && !strstr(raw,"standard stream restoration incomplete") && access("forbidden-js",F_OK)!=0;
  printf("{\"case_id\":\"cli.signal.%s.mixed\",\"status\":\"%s\",\"actual\":{\"ready\":%s,\"signal\":%d,\"signal_return\":%d,\"owner_pid\":%d,\"direct_pid\":%d,\"native_identity_before_stop\":%s,\"exit_code\":%d,\"supervisor_timeout\":%s,\"elapsed_after_signal_ms\":%.6f,\"full_modes_and_flags_restored\":%s,\"terminal_reader_joined\":%s,\"sdk_native_reap_close_quiesce_destroy_observed\":%s,\"native_cleanup_complete\":%s}}\n",
         number==SIGINT ? "SIGINT":"SIGTERM",good ? "pass":"fail",boolean(ready),number,sent,child,direct,boolean(identity),code,boolean(timeout),milliseconds()-start,boolean(modes),boolean(joined),boolean(proof),boolean(good));
  free(native);free(raw);return !good;
}

int main(int argc,char **argv) {
  const char *temporary=getenv("TMPDIR");if(temporary && chdir(temporary))return 125;
  if(argc==7 && !strcmp(argv[1],"--signal")) {
    if(strcmp(argv[5],"SIGINT") && strcmp(argv[5],"SIGTERM"))return 64;
    if(!strcmp(argv[6],"mixed"))return mixed_signal_case(argv[2],argv[3],argv[4],!strcmp(argv[5],"SIGINT") ? SIGINT:SIGTERM);
    if(strcmp(argv[6],"wait") && strcmp(argv[6],"execute") && strcmp(argv[6],"output") && strcmp(argv[6],"help") && strcmp(argv[6],"strong") && strcmp(argv[6],"queue") && strcmp(argv[6],"budget") && strcmp(argv[6],"late"))return 64;
    return signal_case(argv[2],argv[3],argv[4],!strcmp(argv[5],"SIGINT") ? SIGINT:SIGTERM,argv[6]);
  }
  if(argc==6 && !strcmp(argv[1],"--route")) {
    if(!strcmp(argv[2],"pipe-binary-background") || !strcmp(argv[2],"large-result-restored"))return binary_pipe_case(argv[3],argv[4],!strcmp(argv[2],"large-result-restored"));
    return file_route(argv[2],argv[3],argv[4],argv[5]);
  }
  fprintf(stderr,"usage: guest-cli --signal CLI LOADER HELPER SIGINT|SIGTERM MODE\n");return 64;
}
