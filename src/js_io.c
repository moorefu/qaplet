/* SPDX-License-Identifier: MIT */
#include "js_io.h"
#include "io_internal.h"
#include <math.h>

typedef struct js_endpoint js_endpoint;
typedef struct js_io_task js_io_task;
struct js_endpoint {
  js_endpoint *next;
  qaplet_io_endpoint token;
  unsigned refs;
  int writing, closed, checkpoint;
  JSValue resolve, reject;
};
struct js_io_task {
  js_io_task *next;
  qaplet_io_operation token;
  js_endpoint *endpoint;
  unsigned refs;
  int terminal, checkpoint;
  qaplet_code code;
  JSValue resolve, reject;
};
static JSClassID endpoint_class,task_class;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void allocate_classes(void){JS_NewClassID(&endpoint_class);JS_NewClassID(&task_class);}
static JSValue error_value(JSContext *c,qaplet_code code,uint32_t domain,int error,size_t bytes) {
  JSValue v=JS_NewError(c);if(JS_IsException(v))return v;
  if(JS_DefinePropertyValueStr(c,v,"code",JS_NewInt32(c,code),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"errorDomain",JS_NewUint32(c,domain),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"error",JS_NewInt32(c,error),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"bytesWritten",JS_NewFloat64(c,bytes),JS_PROP_C_W_E)<0 ||
     JS_DefinePropertyValueStr(c,v,"message",JS_NewString(c,"IO operation failed"),JS_PROP_C_W_E)<0){JS_FreeValue(c,v);return JS_EXCEPTION;}
  return v;
}
static JSValue fail(JSContext *c,qaplet_code code){if(JS_HasException(c))return JS_EXCEPTION;JSValue v=error_value(c,code,0,0,0);return JS_IsException(v) ? v : JS_Throw(c,v);}
static void settle(qaplet_runtime *r,JSValue function,JSValue value) {
  JSValue result=JS_IsException(value) ? JS_EXCEPTION : JS_Call(r->context,function,JS_UNDEFINED,1,&value);JS_FreeValue(r->context,value);
  if(JS_IsException(result)){JSValue exception=JS_GetException(r->context);if(!qaplet_execution_check(r) && !r->execution_code){qaplet_capture_exception(r,exception);r->execution_code=qaplet_module_exception_code(r,exception);}JS_FreeValue(r->context,exception);}JS_FreeValue(r->context,result);
}
static void release_endpoint(JSContext *c,js_endpoint *e){if(!--e->refs)js_free(c,e);}
static void endpoint_finalizer(JSRuntime *engine,JSValue value){js_endpoint *e=JS_GetOpaque(value,endpoint_class);if(e && !--e->refs)js_free_rt(engine,e);}
static void task_finalizer(JSRuntime *engine,JSValue value){js_io_task *t=JS_GetOpaque(value,task_class);if(t && !--t->refs)js_free_rt(engine,t);}
static int promise_property(JSContext *c,JSValue object,const char *name,JSValue *resolve,JSValue *reject){JSValue callbacks[2],promise=JS_NewPromiseCapability(c,callbacks);if(JS_IsException(promise))return -1;*resolve=callbacks[0];*reject=callbacks[1];return JS_DefinePropertyValueStr(c,object,name,promise,JS_PROP_ENUMERABLE);}
static void refresh_closed(qaplet_runtime *r,js_endpoint *e,unsigned delivering) {
  if(e->closed)return;qaplet_io_endpoint_status status=QAPLET_IO_ENDPOINT_STATUS_INIT;
  if(qaplet_get_io_endpoint_status(r,&e->token,&status,NULL) || !status.ended || status.physical_pending || status.pending_handoffs>delivering)return;
  e->closed=1;e->checkpoint=1;settle(r,e->resolve,JS_UNDEFINED);
  if(r->io_js_closed)r->io_js_closed(r,&e->token);
}
static JSValue task_cancel(JSContext *c,JSValueConst self,int argc,JSValueConst *argv) {
  (void)argc;(void)argv;js_io_task *t=JS_GetOpaque(self,task_class);if(!t)return fail(c,QAPLET_INVALID_HANDLE);
  qaplet_runtime *r=JS_GetContextOpaque(c);qaplet_code code=t->terminal ? (t->code==QAPLET_CANCELLED ? QAPLET_OK : QAPLET_ALREADY_COMPLETED) : qaplet_io_cancel(r,&t->token,NULL);
  return code ? fail(c,code) : JS_UNDEFINED;
}
static qaplet_code input_bytes(qaplet_runtime *r,JSValueConst value,JSValue *buffer,unsigned char **bytes,size_t *length) {
  JSContext *c=r->context;if(!JS_IsObject(value) || JS_GetClassID(value)!=r->uint8_array_class)return QAPLET_VALUE_TYPE_UNSUPPORTED;
  size_t offset,ignored,element,backing;*buffer=JS_GetTypedArrayBuffer(c,value,&offset,&ignored,&element);
  if(JS_IsException(*buffer)){JS_FreeValue(c,JS_GetException(c));*buffer=JS_UNDEFINED;return QAPLET_VALUE_TYPE_UNSUPPORTED;}
  if(JS_GetClassID(*buffer)!=r->array_buffer_class)return QAPLET_VALUE_TYPE_UNSUPPORTED;
  JSValue size=JS_Call(c,r->typed_array_byte_length,value,0,NULL);double count=0;
  if(JS_IsException(size) || JS_ToFloat64(c,&count,size)){JS_FreeValue(c,size);return QAPLET_MEMORY_LIMIT;}JS_FreeValue(c,size);
  if(count>r->m2_limits.io_block_bytes)return QAPLET_VALUE_TOO_LARGE;
  *bytes=JS_GetArrayBuffer(c,&backing,*buffer);*length=(size_t)count;
  if(offset>backing || *length>backing-offset || (!*bytes && *length))return QAPLET_VALUE_TYPE_UNSUPPORTED;
  if(*length)*bytes+=offset;return QAPLET_OK;
}
static JSValue endpoint_operation(JSContext *c,JSValueConst self,int argc,JSValueConst *argv,int writing) {
  qaplet_runtime *r=JS_GetContextOpaque(c);js_endpoint *e=JS_GetOpaque(self,endpoint_class);
  if(!e)return fail(c,QAPLET_INVALID_HANDLE);if(e->closed || e->writing!=writing)return fail(c,QAPLET_BAD_STATE);
  if(argc!=1)return fail(c,QAPLET_INVALID_ARGUMENT);
  qaplet_code code=QAPLET_OK;size_t length=0;unsigned char *bytes=NULL;JSValue buffer=JS_UNDEFINED;
  if(writing)code=input_bytes(r,argv[0],&buffer,&bytes,&length);
  else {double n=0;if(!JS_IsNumber(argv[0]) || JS_ToFloat64(c,&n,argv[0]) || !isfinite(n) || n<1 || floor(n)!=n)code=QAPLET_INVALID_ARGUMENT;else if(n>r->m2_limits.io_block_bytes)code=QAPLET_VALUE_TOO_LARGE;else length=(size_t)n;}
  JSValue object=JS_UNDEFINED;js_io_task *t=NULL;
  if(!code){object=JS_NewObjectClass(c,task_class);if(JS_IsException(object))code=QAPLET_MEMORY_LIMIT;}
  if(!code){t=js_mallocz(c,sizeof(*t));if(!t)code=QAPLET_MEMORY_LIMIT;else {t->refs=1;t->resolve=t->reject=JS_UNDEFINED;t->token=(qaplet_io_operation)QAPLET_IO_OPERATION_INIT;JS_SetOpaque(object,t);if(promise_property(c,object,"promise",&t->resolve,&t->reject)<0)code=QAPLET_MEMORY_LIMIT;}}
  if(!code){r->bridge_active=1;code=writing ? qaplet_io_write(r,&e->token,bytes,length,&t->token,NULL) : qaplet_io_read(r,&e->token,length,&t->token,NULL);r->bridge_active=0;}
  JS_FreeValue(c,buffer);
  if(code){if(t){JS_FreeValue(c,t->resolve);JS_FreeValue(c,t->reject);}JS_FreeValue(c,object);return fail(c,code);}
  t->endpoint=e;++e->refs;++t->refs;t->next=r->js_io_tasks;r->js_io_tasks=t;return object;
}
static JSValue endpoint_control(JSContext *c,JSValueConst self,int argc,JSValueConst *argv,int magic) {
  (void)argc;(void)argv;qaplet_runtime *r=JS_GetContextOpaque(c);js_endpoint *e=JS_GetOpaque(self,endpoint_class);if(!e)return fail(c,QAPLET_INVALID_HANDLE);
  if(magic==4){qaplet_io_endpoint_status status=QAPLET_IO_ENDPOINT_STATUS_INIT;return JS_NewBool(c,!e->closed && !qaplet_get_io_endpoint_status(r,&e->token,&status,NULL) && status.referenced);}
  if(e->closed)return JS_UNDEFINED;
  r->bridge_active=1;qaplet_code code=magic==0 ? qaplet_io_close(r,&e->token,NULL) : magic==1 ? qaplet_io_finish(r,&e->token,NULL) : qaplet_io_set_ref(r,&e->token,magic==2,NULL);r->bridge_active=0;
  if(!code)refresh_closed(r,e,0);return code ? fail(c,code) : JS_UNDEFINED;
}
static int deliver_io(qaplet_runtime *r,const qaplet_io_operation *token) {
  js_io_task *t=r->js_io_tasks;for(;t;t=t->next)if(t->token.slot==token->slot && t->token.generation==token->generation)break;if(!t)return 0;
  qaplet_code code=QAPLET_OK;if(!r->execution_active){qaplet_execution_budget b;code=qaplet_execution_prepare(r,NULL,&b);if(!code)code=qaplet_execution_begin(r,&b);if(!code)r->module_execution=0;}
  if(code || qaplet_execution_dispatch(r))return -1;
  qaplet_io_status status=QAPLET_IO_STATUS_INIT;if(qaplet_get_io_status(r,token,&status,NULL))return -1;
  JSContext *c=r->context;unsigned char *data=NULL;
  if(!t->endpoint->writing && status.length){data=js_malloc(c,status.length);if(!data){settle(r,t->reject,JS_EXCEPTION);t->terminal=t->checkpoint=1;t->code=QAPLET_MEMORY_LIMIT;return 1;}}
  qaplet_io_result result=QAPLET_IO_RESULT_INIT;result.buffer=data;result.capacity=t->endpoint->writing ? 0 : status.length;
  r->bridge_active=1;code=qaplet_io_copy_result(r,token,&result,NULL);r->bridge_active=0;
  JSValue value;
  if(code || result.code)value=error_value(c,code ? code : result.code,result.error_domain,result.error,result.length);
  else {
    value=JS_NewObject(c);
    if(!JS_IsException(value) && t->endpoint->writing){if(JS_DefinePropertyValueStr(c,value,"bytesWritten",JS_NewFloat64(c,result.length),JS_PROP_C_W_E)<0){JS_FreeValue(c,value);value=JS_EXCEPTION;}}
    else if(!JS_IsException(value)){
      JSValue backing=JS_NewArrayBufferCopy(c,data,result.length),args[]={backing,JS_UNDEFINED,JS_UNDEFINED};
      JSValue bytes=JS_IsException(backing) ? JS_EXCEPTION : JS_NewTypedArray(c,3,args,JS_TYPED_ARRAY_UINT8);JS_FreeValue(c,backing);
      if(JS_IsException(bytes) || JS_DefinePropertyValueStr(c,value,"bytes",bytes,JS_PROP_C_W_E)<0 || JS_DefinePropertyValueStr(c,value,"eof",JS_NewBool(c,result.eof),JS_PROP_C_W_E)<0){JS_FreeValue(c,value);value=JS_EXCEPTION;}
    }
  }
  js_free(c,data);t->terminal=t->checkpoint=1;t->code=code ? code : result.code;
  settle(r,t->code ? t->reject : t->resolve,value);refresh_closed(r,t->endpoint,1);return 1;
}
void qaplet_js_io_checkpoint(qaplet_runtime *r) {
  qaplet_io_js_checkpoint(r);
  js_io_task **at=(js_io_task **)&r->js_io_tasks;
  while(*at){js_io_task *t=*at;if(!t->checkpoint){at=&t->next;continue;}*at=t->next;
    r->bridge_active=1;qaplet_io_release(r,&t->token,NULL);r->bridge_active=0;
    JS_FreeValue(r->context,t->resolve);JS_FreeValue(r->context,t->reject);release_endpoint(r->context,t->endpoint);if(!--t->refs)js_free(r->context,t);
  }
  js_endpoint **ep=(js_endpoint **)&r->js_io_endpoints;
  while(*ep){js_endpoint *e=*ep;if(!e->checkpoint){ep=&e->next;continue;}*ep=e->next;JS_FreeValue(r->context,e->resolve);JS_FreeValue(r->context,e->reject);release_endpoint(r->context,e);}
}
void qaplet_js_io_shutdown(qaplet_runtime *r) {
  js_io_task *t=r->js_io_tasks;r->js_io_tasks=NULL;
  while(t){js_io_task *next=t->next;JS_FreeValue(r->context,t->resolve);JS_FreeValue(r->context,t->reject);t->terminal=1;t->code=QAPLET_CANCELLED;release_endpoint(r->context,t->endpoint);if(!--t->refs)js_free(r->context,t);t=next;}
  js_endpoint *e=r->js_io_endpoints;r->js_io_endpoints=NULL;
  while(e){js_endpoint *next=e->next;JS_FreeValue(r->context,e->resolve);JS_FreeValue(r->context,e->reject);e->closed=1;release_endpoint(r->context,e);e=next;}
}
static const JSCFunctionListEntry endpoint_methods[]={JS_CFUNC_MAGIC_DEF("read",1,endpoint_operation,0),JS_CFUNC_MAGIC_DEF("write",1,endpoint_operation,1),JS_CFUNC_MAGIC_DEF("close",0,endpoint_control,0),JS_CFUNC_MAGIC_DEF("finish",0,endpoint_control,1),JS_CFUNC_MAGIC_DEF("ref",0,endpoint_control,2),JS_CFUNC_MAGIC_DEF("unref",0,endpoint_control,3),JS_CFUNC_MAGIC_DEF("hasRef",0,endpoint_control,4)};
JSValue qaplet_js_io_endpoint(qaplet_runtime *r,const qaplet_io_endpoint *token,int writing) {
  JSContext *c=r->context;pthread_once(&once,allocate_classes);
  if(!JS_IsRegisteredClass(r->engine,endpoint_class)){
    JSClassDef e={"QapletByteEndpoint",.finalizer=endpoint_finalizer},t={"QapletIOTask",.finalizer=task_finalizer};
    if(JS_NewClass(r->engine,endpoint_class,&e)<0 || JS_NewClass(r->engine,task_class,&t)<0)return JS_EXCEPTION;
    JSValue prototype=JS_NewObject(c);if(JS_IsException(prototype))return prototype;
    if(JS_SetPropertyFunctionList(c,prototype,endpoint_methods,sizeof(endpoint_methods)/sizeof(*endpoint_methods))<0){JS_FreeValue(c,prototype);return JS_EXCEPTION;}JS_SetClassProto(c,endpoint_class,prototype);
    prototype=JS_NewObject(c);if(JS_IsException(prototype))return prototype;
    if(JS_SetPropertyStr(c,prototype,"cancel",JS_NewCFunction(c,task_cancel,"cancel",0))<0){JS_FreeValue(c,prototype);return JS_EXCEPTION;}JS_SetClassProto(c,task_class,prototype);
  }
  JSValue object=JS_NewObjectClass(c,endpoint_class);if(JS_IsException(object))return object;
  js_endpoint *e=js_mallocz(c,sizeof(*e));if(!e){JS_FreeValue(c,object);return JS_EXCEPTION;}
  e->token=*token;e->writing=writing;e->refs=1;e->resolve=e->reject=JS_UNDEFINED;JS_SetOpaque(object,e);
  if(promise_property(c,object,"closed",&e->resolve,&e->reject)<0){JS_FreeValue(c,e->resolve);JS_FreeValue(c,e->reject);JS_FreeValue(c,object);return JS_EXCEPTION;}
  ++e->refs;e->next=r->js_io_endpoints;r->js_io_endpoints=e;r->io_js_deliver=deliver_io;return object;
}
