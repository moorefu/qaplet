/* SPDX-License-Identifier: MIT */
#include "runtime_internal.h"
#include "js_io.h"
#include <math.h>
#include <string.h>

typedef struct js_process js_process;
struct js_process {
  js_process *next;
  qaplet_process token;
  unsigned refs;
  int exited, closed, checkpoint;
  qaplet_code code;
  qaplet_io_endpoint stdio[3];
  unsigned ended[3];
  JSValue exit_resolve, exit_reject, close_resolve, close_reject;
};
static JSClassID process_class;
static pthread_once_t classes_once=PTHREAD_ONCE_INIT;
static void allocate_classes(void) {JS_NewClassID(&process_class);}
static JSValue operation_error(JSContext *c,qaplet_code code,uint32_t domain,int error) {
  JSValue v=JS_NewError(c);
  if(JS_IsException(v))return v;
  if(JS_DefinePropertyValueStr(c,v,"code",JS_NewInt32(c,code),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"errorDomain",JS_NewUint32(c,domain),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"error",JS_NewInt32(c,error),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"message",JS_NewString(c,"Asynchronous operation failed"),JS_PROP_C_W_E)<0){JS_FreeValue(c,v);return JS_EXCEPTION;}
  return v;
}
static JSValue fail(JSContext *c,qaplet_code code) {
  if(JS_HasException(c))return JS_EXCEPTION;
  JSValue error=operation_error(c,code,0,0);
  return JS_IsException(error) ? error : JS_Throw(c,error);
}
static void settle(qaplet_runtime *r,JSValue function,JSValue value) {
  JSValue result=JS_IsException(value) ? JS_EXCEPTION : JS_Call(r->context,function,JS_UNDEFINED,1,&value);
  JS_FreeValue(r->context,value);
  if(JS_IsException(result)){
    JSValue exception=JS_GetException(r->context);
    if(!qaplet_execution_check(r) && !r->execution_code){qaplet_capture_exception(r,exception);r->execution_code=qaplet_module_exception_code(r,exception);}
    JS_FreeValue(r->context,exception);
  }
  JS_FreeValue(r->context,result);
}
static int begin_delivery(qaplet_runtime *r) {
  qaplet_code code=QAPLET_OK;
  if(!r->execution_active){qaplet_execution_budget b;code=qaplet_execution_prepare(r,NULL,&b);if(!code)code=qaplet_execution_begin(r,&b);if(!code)r->module_execution=0;}
  return !code && !qaplet_execution_dispatch(r);
}
static void free_process_values(qaplet_runtime *r,js_process *p) {
  JS_FreeValue(r->context,p->exit_resolve);JS_FreeValue(r->context,p->exit_reject);
  JS_FreeValue(r->context,p->close_resolve);JS_FreeValue(r->context,p->close_reject);
  p->exit_resolve=p->exit_reject=p->close_resolve=p->close_reject=JS_UNDEFINED;
}
static void process_finalizer(JSRuntime *engine,JSValue value) {
  js_process *p=JS_GetOpaque(value,process_class);if(p && !--p->refs)js_free_rt(engine,p);
}
static JSValue process_method(JSContext *c,JSValueConst self,int argc,JSValueConst *argv,int magic) {
  (void)argc;(void)argv;qaplet_runtime *r=JS_GetContextOpaque(c);js_process *p=JS_GetOpaque(self,process_class);
  if(!p)return fail(c,QAPLET_INVALID_HANDLE);
  qaplet_code code=QAPLET_OK;
  if(!magic)code=p->exited ? (p->code==QAPLET_CANCELLED ? QAPLET_OK : QAPLET_ALREADY_COMPLETED) : qaplet_process_cancel(r,&p->token,NULL);
  else if(magic==3){qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;return JS_NewBool(c,!p->closed && !qaplet_get_process_status(r,&p->token,&status,NULL) && status.referenced);}
  else if(!p->closed){r->bridge_active=1;code=qaplet_process_set_ref(r,&p->token,magic==1,NULL);r->bridge_active=0;}
  return code ? fail(c,code) : JS_UNDEFINED;
}
static void process_closed(qaplet_runtime *r,js_process *p) {
  if(!p->closed && p->exited && p->ended[0] && p->ended[1] && p->ended[2]){p->closed=p->checkpoint=1;settle(r,p->close_resolve,JS_UNDEFINED);}
}
static void endpoint_closed(qaplet_runtime *r,const qaplet_io_endpoint *token) {
  for(js_process *p=r->js_processes;p;p=p->next)for(int i=0;i<3;++i)
    if(p->stdio[i].runtime_id==token->runtime_id && p->stdio[i].slot==token->slot && p->stdio[i].generation==token->generation){p->ended[i]=1;process_closed(r,p);}
}
static int deliver_process(qaplet_runtime *r,const qaplet_process *token) {
  js_process *p=r->js_processes;
  for(;p;p=p->next)if(p->token.slot==token->slot && p->token.generation==token->generation)break;
  if(!p)return 0;if(!begin_delivery(r))return -1;
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  qaplet_code code=qaplet_get_process_status(r,token,&status,NULL);if(code)return -1;
  p->exited=1;p->code=status.code;
  JSValue value=status.code ? operation_error(r->context,status.code,status.error_domain,status.error) : JS_NewObject(r->context);
  if(!JS_IsException(value)){
    if(JS_DefinePropertyValueStr(r->context,value,"code",JS_NewInt32(r->context,status.code),JS_PROP_C_W_E)<0 ||
       JS_DefinePropertyValueStr(r->context,value,"exited",JS_NewBool(r->context,status.exited),JS_PROP_C_W_E)<0 ||
       JS_DefinePropertyValueStr(r->context,value,"reaped",JS_NewBool(r->context,status.reaped),JS_PROP_C_W_E)<0 ||
       JS_DefinePropertyValueStr(r->context,value,"exitCode",JS_NewUint32(r->context,status.native_exit_code),JS_PROP_C_W_E)<0 ||
       JS_DefinePropertyValueStr(r->context,value,"signal",JS_NewUint32(r->context,status.native_signal),JS_PROP_C_W_E)<0 ||
       JS_DefinePropertyValueStr(r->context,value,"errorDomain",JS_NewUint32(r->context,status.error_domain),JS_PROP_C_W_E)<0 ||
       JS_DefinePropertyValueStr(r->context,value,"error",JS_NewInt32(r->context,status.error),JS_PROP_C_W_E)<0){JS_FreeValue(r->context,value);value=JS_EXCEPTION;}
  }
  settle(r,status.code ? p->exit_reject : p->exit_resolve,value);
  process_closed(r,p);
  return 1;
}
void qaplet_js_async_checkpoint(qaplet_runtime *r) {
  qaplet_process_js_checkpoint(r);
  js_process **at=(js_process **)&r->js_processes;
  while(*at){js_process *p=*at;if(!p->checkpoint){at=&p->next;continue;}
    r->bridge_active=1;qaplet_code code=qaplet_process_release(r,&p->token,NULL);r->bridge_active=0;
    if(code==QAPLET_BUSY){at=&p->next;continue;}
    *at=p->next;free_process_values(r,p);if(!--p->refs)js_free(r->context,p);
  }
}
void qaplet_js_async_shutdown(qaplet_runtime *r) {
  js_process *p=r->js_processes;r->js_processes=NULL;
  while(p){js_process *next=p->next;free_process_values(r,p);p->closed=1;if(!--p->refs)js_free(r->context,p);p=next;}
}
static qaplet_code string(qaplet_runtime *r,JSValueConst value,qaplet_string *out) {
  if(!JS_IsString(value))return QAPLET_INVALID_ARGUMENT;
  qaplet_code code=qaplet_js_string_size(r,value,&out->length);if(code)return code;
  out->data=JS_ToCStringLen(r->context,&out->length,value);return out->data ? QAPLET_OK : QAPLET_MEMORY_LIMIT;
}
static void free_strings(JSContext *c,qaplet_string *v,size_t n) {for(size_t i=0;i<n;++i)JS_FreeCString(c,v[i].data);js_free(c,v);}
static qaplet_code strings(qaplet_runtime *r,JSValueConst v,qaplet_string **out,size_t *count,uint64_t maximum) {
  JSContext *c=r->context;if(!JS_IsArray(c,v))return QAPLET_INVALID_ARGUMENT;
  JSValue length=JS_GetPropertyStr(c,v,"length");uint32_t n=0;
  if(JS_IsException(length) || JS_ToUint32(c,&n,length)){JS_FreeValue(c,length);return QAPLET_INVALID_ARGUMENT;}JS_FreeValue(c,length);
  if(n>maximum || n>r->m2_limits.process_data_bytes)return QAPLET_RESOURCE_LIMIT;
  qaplet_string *items=js_mallocz(c,(n ? n : 1)*sizeof(*items));if(!items)return QAPLET_MEMORY_LIMIT;
  qaplet_code code=QAPLET_OK;size_t i=0;
  for(;i<n;++i){JSValue item=JS_GetPropertyUint32(c,v,i);code=string(r,item,&items[i]);JS_FreeValue(c,item);if(code)break;}
  if(code){free_strings(c,items,i);return code;}*out=items;*count=n;return QAPLET_OK;
}
static qaplet_code environment(qaplet_runtime *r,JSValueConst v,qaplet_string **out,size_t *count) {
  JSContext *c=r->context;
  if(JS_IsArray(c,v))return strings(r,v,out,count,r->m2_limits.process_data_bytes);
  if(!JS_IsObject(v) || JS_IsNull(v))return QAPLET_INVALID_ARGUMENT;
  JSPropertyEnum *keys=NULL;uint32_t n=0;
  if(JS_GetOwnPropertyNames(c,&keys,&n,v,JS_GPN_STRING_MASK|JS_GPN_ENUM_ONLY)<0)return QAPLET_JS_EXCEPTION;
  qaplet_code code=n>r->m2_limits.process_data_bytes ? QAPLET_RESOURCE_LIMIT : QAPLET_OK;
  qaplet_string *items=NULL;size_t used=0,total=0;
  if(!code){items=js_mallocz(c,(n ? n : 1)*sizeof(*items));if(!items)code=QAPLET_MEMORY_LIMIT;}
  for(uint32_t i=0;i<n && !code;++i){
    JSValue key=JS_AtomToValue(c,keys[i].atom),value=JS_GetProperty(c,v,keys[i].atom);size_t key_bytes=0,value_bytes=0;
    if(!JS_IsString(value))code=QAPLET_INVALID_ARGUMENT;
    if(!code)code=qaplet_js_string_size(r,key,&key_bytes);
    if(!code)code=qaplet_js_string_size(r,value,&value_bytes);
    if(!code && (key_bytes>r->m2_limits.process_data_bytes-total || value_bytes+2>r->m2_limits.process_data_bytes-total-key_bytes))code=QAPLET_RESOURCE_LIMIT;
    if(!code){total+=key_bytes+value_bytes+2;const char *key_text=JS_ToCStringLen(c,&key_bytes,key),*value_text=JS_ToCStringLen(c,&value_bytes,value);
      if(!key_text || !value_text)code=QAPLET_MEMORY_LIMIT;
      else if(!key_bytes || memchr(key_text,'=',key_bytes) || memchr(key_text,0,key_bytes))code=QAPLET_INVALID_ARGUMENT;
      if(!code){char *text=js_malloc(c,key_bytes+value_bytes+1);if(!text)code=QAPLET_MEMORY_LIMIT;
        else {memcpy(text,key_text,key_bytes);text[key_bytes]='=';memcpy(text+key_bytes+1,value_text,value_bytes);JSValue pair=JS_NewStringLen(c,text,key_bytes+value_bytes+1);js_free(c,text);
          if(JS_IsException(pair))code=QAPLET_MEMORY_LIMIT;else {items[used].data=JS_ToCStringLen(c,&items[used].length,pair);if(!items[used].data)code=QAPLET_MEMORY_LIMIT;}JS_FreeValue(c,pair);if(!code)++used;
        }
      }
      JS_FreeCString(c,key_text);JS_FreeCString(c,value_text);
    }
    JS_FreeValue(c,key);JS_FreeValue(c,value);
  }
  for(uint32_t i=0;i<n;++i)JS_FreeAtom(c,keys[i].atom);js_free(c,keys);
  if(code){free_strings(c,items,used);return code;}*out=items;*count=n;return QAPLET_OK;
}
static int promise_property(JSContext *c,JSValue object,const char *name,JSValue *resolve,JSValue *reject) {
  JSValue callbacks[2],promise=JS_NewPromiseCapability(c,callbacks);
  if(JS_IsException(promise))return -1;
  *resolve=callbacks[0];*reject=callbacks[1];return JS_DefinePropertyValueStr(c,object,name,promise,JS_PROP_ENUMERABLE);
}
static JSValue js_spawn(JSContext *c,JSValueConst self,int argc,JSValueConst *argv) {
  (void)self;qaplet_runtime *r=JS_GetContextOpaque(c);
  if(argc<1 || argc>2)return fail(c,QAPLET_INVALID_ARGUMENT);
  qaplet_process_options o=QAPLET_PROCESS_OPTIONS_INIT;
  qaplet_code code=string(r,argv[0],&o.path);qaplet_string *args=NULL,*env=NULL;
  if(!code && argc==2 && (!JS_IsObject(argv[1]) || JS_IsNull(argv[1])))code=QAPLET_INVALID_ARGUMENT;
  const char *names[]={"argv","env","cwd","stdio"};
  for(int i=0;i<4 && !code && argc==2;++i){JSValue v=JS_GetPropertyStr(c,argv[1],names[i]);
    if(JS_IsException(v))code=QAPLET_JS_EXCEPTION;
    else if(!JS_IsUndefined(v)){
      if(i==0){code=strings(r,v,&args,&o.argc,r->m2_limits.process_arguments);o.argv=args;}
      if(i==1){code=environment(r,v,&env,&o.env_count);o.env=env;o.flags|=QAPLET_PROCESS_REPLACE_ENV;}
      if(i==2)code=string(r,v,&o.cwd);
      if(i==3){
        if(!JS_IsArray(c,v))code=QAPLET_INVALID_ARGUMENT;
        else {JSValue size=JS_GetPropertyStr(c,v,"length");uint32_t n=0;if(JS_IsException(size) || JS_ToUint32(c,&n,size) || n!=3)code=QAPLET_INVALID_ARGUMENT;JS_FreeValue(c,size);}
        for(int j=0;j<3 && !code;++j){JSValue mode=JS_GetPropertyUint32(c,v,j);qaplet_string text={0};code=string(r,mode,&text);
          if(!code){if(text.length==4 && !memcmp(text.data,"pipe",4))o.stdio[j]=QAPLET_PROCESS_PIPE;else if(text.length==7 && !memcmp(text.data,"inherit",7))o.stdio[j]=QAPLET_PROCESS_INHERIT;else if(text.length==6 && !memcmp(text.data,"ignore",6))o.stdio[j]=QAPLET_PROCESS_IGNORE;else code=QAPLET_INVALID_ARGUMENT;}
          JS_FreeCString(c,text.data);JS_FreeValue(c,mode);
        }
      }
    }JS_FreeValue(c,v);
  }
  JSValue object=JS_UNDEFINED;js_process *p=NULL;
  if(!code){object=JS_NewObjectClass(c,process_class);if(JS_IsException(object))code=QAPLET_MEMORY_LIMIT;}
  if(!code){p=js_mallocz(c,sizeof(*p));if(!p)code=QAPLET_MEMORY_LIMIT;
    else {p->refs=1;p->token=(qaplet_process)QAPLET_PROCESS_INIT;JS_SetOpaque(object,p);p->exit_resolve=p->exit_reject=p->close_resolve=p->close_reject=JS_UNDEFINED;
      if(promise_property(c,object,"exited",&p->exit_resolve,&p->exit_reject)<0 || promise_property(c,object,"closed",&p->close_resolve,&p->close_reject)<0)code=QAPLET_MEMORY_LIMIT;}
  }
  qaplet_process_status status=QAPLET_PROCESS_STATUS_INIT;
  if(!code){r->bridge_active=1;code=qaplet_process_spawn(r,&o,&p->token,&status,NULL);r->bridge_active=0;}
  JS_FreeCString(c,o.path.data);JS_FreeCString(c,o.cwd.data);free_strings(c,args,o.argc);free_strings(c,env,o.env_count);
  if(code){if(p)free_process_values(r,p);JS_FreeValue(c,object);if(status.error_domain){JSValue error=operation_error(c,code,status.error_domain,status.error);return JS_IsException(error) ? error : JS_Throw(c,error);}return fail(c,code);}
  ++p->refs;p->next=r->js_processes;r->js_processes=p;
  const char *stream_names[]={"stdin","stdout","stderr"};
  for(int i=0;i<3 && !code;++i){JSValue endpoint=JS_NULL;
    if(o.stdio[i]==QAPLET_PROCESS_PIPE){p->stdio[i]=(qaplet_io_endpoint)QAPLET_IO_ENDPOINT_INIT;r->bridge_active=1;code=qaplet_process_get_stdio(r,&p->token,i,&p->stdio[i],NULL);r->bridge_active=0;
      if(!code){endpoint=qaplet_js_io_endpoint(r,&p->stdio[i],i==0);if(JS_IsException(endpoint))code=QAPLET_MEMORY_LIMIT;}}
    else p->ended[i]=1;
    if(!code && JS_DefinePropertyValueStr(c,object,stream_names[i],endpoint,JS_PROP_ENUMERABLE)<0)code=QAPLET_MEMORY_LIMIT;
    else if(code)JS_FreeValue(c,endpoint);
  }
  if(code){qaplet_process_cancel(r,&p->token,NULL);JS_FreeValue(c,object);return fail(c,code);}return object;
}
static int initialize_process(JSContext *c,JSModuleDef *module) {return JS_SetModuleExport(c,module,"spawn",JS_NewCFunction(c,js_spawn,"spawn",2));}
static const JSCFunctionListEntry process_methods[]={JS_CFUNC_MAGIC_DEF("cancel",0,process_method,0),JS_CFUNC_MAGIC_DEF("ref",0,process_method,1),JS_CFUNC_MAGIC_DEF("unref",0,process_method,2),JS_CFUNC_MAGIC_DEF("hasRef",0,process_method,3)};
JSModuleDef *qaplet_process_load(qaplet_runtime *r) {
  pthread_once(&classes_once,allocate_classes);JSClassDef definition={"QapletProcess",.finalizer=process_finalizer};
  if(JS_NewClass(r->engine,process_class,&definition)<0)return NULL;
  JSValue prototype=JS_NewObject(r->context);if(JS_IsException(prototype))return NULL;
  if(JS_SetPropertyFunctionList(r->context,prototype,process_methods,sizeof(process_methods)/sizeof(*process_methods))<0){JS_FreeValue(r->context,prototype);return NULL;}
  JS_SetClassProto(r->context,process_class,prototype);r->process_js_deliver=deliver_process;
  r->io_js_closed=endpoint_closed;
  JSModuleDef *module=JS_NewCModule(r->context,"qaplet:process",initialize_process);
  if(module && JS_AddModuleExport(r->context,module,"spawn")<0)return NULL;return module;
}
