/* SPDX-License-Identifier: MIT */
#include "qaplet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"runtime snapshot check failed: %s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)

static qaplet_code settle(qaplet_runtime *runtime, qaplet_progress *progress, qaplet_code code) {
  for (unsigned i=0;!code&&progress->phase==QAPLET_PHASE_PENDING&&i<4096;++i)
    code=qaplet_pump(runtime,progress,NULL);
  return code;
}

static int release(qaplet_runtime *runtime) {
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK&&qaplet_destroy(runtime,NULL)==QAPLET_OK);
  return 0;
}

int sdk_runtime(const char *root) {
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  const char source[] = "import('qaplet:runtime').then(m=>Array.isArray(m.argv)&&m.argv.length===0&&Object.isFrozen(m.argv)&&typeof m.cwd==='string'&&m.cwd.length>0)";
  CHECK(qaplet_eval(runtime,source,sizeof(source)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.type==QAPLET_VALUE_BOOL&&result.boolean);
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK&&qaplet_destroy(runtime,NULL)==QAPLET_OK);
  char cwd[4096], expected_cwd[4096], first[]="first", second[]="a\0b";
  CHECK(getcwd(cwd,sizeof(cwd)));
  memcpy(expected_cwd,cwd,strlen(cwd)+1);
  qaplet_string arguments[]={{first,5},{second,3}};
  config.argv=arguments; config.argc=2; config.cwd=cwd; config.cwd_length=strlen(cwd);
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  memset(first,'z',5); memset(second,'z',3); memset(cwd,'z',strlen(cwd));
  const char copied[]="import('qaplet:runtime').then(m=>{let old=m.cwd;try{m.argv[0]='changed'}catch(e){}try{m.argv.push('changed')}catch(e){}try{m.cwd='changed'}catch(e){}return m.argv.length===2&&m.argv[0]==='first'&&m.argv[1]==='a\\0b'&&Object.isFrozen(m.argv)&&m.cwd===old&&m.cwd[0]==='/'})";
  CHECK(qaplet_eval(runtime,copied,sizeof(copied)-1,&progress,NULL)==QAPLET_OK);
  while(progress.phase==QAPLET_PHASE_PENDING) CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.type==QAPLET_VALUE_BOOL&&result.boolean);
  const char read_cwd[]="import('qaplet:runtime').then(m=>m.cwd)";
  CHECK(!chdir(root));
  CHECK(qaplet_eval(runtime,read_cwd,sizeof(read_cwd)-1,&progress,NULL)==QAPLET_OK);
  CHECK(settle(runtime,&progress,QAPLET_OK)==QAPLET_OK);
  char returned[4096]; result.buffer=returned; result.capacity=sizeof(returned);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.length==strlen(expected_cwd)&&!memcmp(returned,expected_cwd,result.length));
  CHECK(!chdir(expected_cwd));
  CHECK(qaplet_close(runtime,NULL)==QAPLET_OK&&qaplet_destroy(runtime,NULL)==QAPLET_OK);
  char *base=realpath(root,NULL); CHECK(base);
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.cwd=base; config.cwd_length=strlen(base);
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  memset(base,'z',strlen(base)); free(base);
  CHECK(qaplet_run_entry(runtime,"entry.mjs",9,&progress,NULL)==QAPLET_OK);
  CHECK(settle(runtime,&progress,QAPLET_OK)==QAPLET_OK&&!release(runtime));
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.argc=1;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_ARGUMENT&&!runtime);
  const char invalid[]={(char)0xc0,(char)0x80};
  qaplet_string bad={invalid,2}; config.argv=&bad;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_UTF8&&!runtime);
  config=(qaplet_config)QAPLET_CONFIG_INIT; config.cwd="relative";config.cwd_length=8;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_ARGUMENT&&!runtime);
  config.cwd="/a\0b";config.cwd_length=4;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_ARGUMENT&&!runtime);
  config.cwd=invalid;config.cwd_length=2;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_UTF8&&!runtime);
  config=(qaplet_config)QAPLET_CONFIG_INIT;config.limits.module_count=1;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_INVALID_ARGUMENT&&!runtime);
  config=(qaplet_config)QAPLET_CONFIG_INIT;config.limits.bridge_value_bytes=4;
  qaplet_string oversized={"12345",5};config.argv=&oversized;config.argc=1;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_VALUE_TOO_LARGE&&!runtime);
  qaplet_abi abi=QAPLET_ABI_INIT;
  CHECK(qaplet_query_abi(&abi)==QAPLET_OK&&(abi.capabilities&QAPLET_CAP_RUNTIME_SNAPSHOT));
  puts("runtime snapshot: fixed read-only argv/cwd; public factory: pass");
  return 0;
}

static qaplet_code entry(qaplet_runtime *runtime,const char *root,const char *file,qaplet_progress *progress) {
  char path[4096];
  if (snprintf(path,sizeof(path),"%s/%s",root,file)>=sizeof(path)) return QAPLET_INVALID_ARGUMENT;
  return settle(runtime,progress,qaplet_run_entry(runtime,path,strlen(path),progress,NULL));
}

int sdk_cli_contract(const char *root) {
  qaplet_config config=QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime=NULL;
  qaplet_progress progress=QAPLET_PROGRESS_INIT;
  qaplet_result result=QAPLET_RESULT_INIT;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  const char source[]="var answer=40;Promise.resolve(answer+2)";
  CHECK(settle(runtime,&progress,qaplet_eval(runtime,source,sizeof(source)-1,&progress,NULL))==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.type==QAPLET_VALUE_DOUBLE&&result.number==42);
  const char hooks[]="globalThis.hooks=0;({get then(){hooks++;throw 1},toString(){hooks++;throw 1}})";
  CHECK(qaplet_eval(runtime,hooks,sizeof(hooks)-1,&progress,NULL)==QAPLET_VALUE_TYPE_UNSUPPORTED);
  const char diagnostic[]="throw Object.defineProperty(new Error(),'message',{get(){hooks++;throw 1}})";
  CHECK(qaplet_eval(runtime,diagnostic,sizeof(diagnostic)-1,&progress,NULL)==QAPLET_JS_EXCEPTION);
  CHECK(qaplet_eval(runtime,"hooks",5,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.number==0);
  CHECK(entry(runtime,root,"cli-cache.mjs",&progress)==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.type==QAPLET_VALUE_UNDEFINED);
  CHECK(entry(runtime,root,"cli-cache.mjs",&progress)==QAPLET_OK);
  CHECK(qaplet_eval(runtime,"cliDependencyRuns",17,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.number==1);
  CHECK(entry(runtime,root,"cli-reject.mjs",&progress)==QAPLET_JS_EXCEPTION);
  CHECK(qaplet_eval(runtime,"42",2,&progress,NULL)==QAPLET_OK);
  CHECK(qaplet_get_result(runtime,&result,NULL)==QAPLET_OK&&result.number==42);
  CHECK(entry(runtime,root,"entry.mjs?refused",&progress)==QAPLET_MODULE_RESOLVE);
  CHECK(!release(runtime));
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  const char pending[]="new Promise(()=>{})";
  CHECK(qaplet_eval(runtime,pending,sizeof(pending)-1,&progress,NULL)==QAPLET_OK&&progress.phase==QAPLET_PHASE_PENDING);
  CHECK(qaplet_pump(runtime,&progress,NULL)==QAPLET_OK&&progress.phase==QAPLET_PHASE_PENDING&&!progress.progressed);
  CHECK(!release(runtime));
  config.limits.execution_dispatches=600;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(entry(runtime,root,"cli-jobs.mjs",&progress)==QAPLET_DISPATCH_LIMIT);
  qaplet_snapshot snapshot=QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK&&snapshot.lifecycle==QAPLET_FAILED&&snapshot.termination_reason==QAPLET_DISPATCH_LIMIT);
  CHECK(!release(runtime));
  config=(qaplet_config)QAPLET_CONFIG_INIT;config.limits.execution_timeout_ms=20;
  CHECK(qaplet_create(&config,&runtime,NULL)==QAPLET_OK);
  CHECK(entry(runtime,root,"cli-loop.mjs",&progress)==QAPLET_DEADLINE_EXCEEDED);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK&&snapshot.lifecycle==QAPLET_FAILED&&snapshot.termination_reason==QAPLET_DEADLINE_EXCEEDED);
  CHECK(!release(runtime));
  puts("CLI contrast: shared Script/ESM/cache/rejection/results/budgets; SDK pending preserved: pass");
  return 0;
}
