/* SPDX-License-Identifier: MIT
 * M0 create/eval/destroy probe only; this is not a Runtime or SDK interface. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "quickjs.h"

static int pointer_roundtrip(void) {
  /* A public-header ABI check. The synthetic pointer is never dereferenced.
     Actual ARM64 malloc previously returned pointers with this bit set. */
#if defined(__aarch64__)
  const uintptr_t sample = UINT64_C(0x800000001230);
#else
  const uintptr_t sample = UINT64_C(0x1230);
#endif
  JSValue value = JS_MKPTR(JS_TAG_OBJECT, (void *)sample);
  return (uintptr_t)JS_VALUE_GET_PTR(value) == sample && JS_VALUE_GET_TAG(value) == JS_TAG_OBJECT;
}

static JSModuleDef *fixture_loader(JSContext *context, const char *name, void *opaque) {
  (void)opaque;
  if (strcmp(name, "m0:dependency")) {
    JS_ThrowReferenceError(context, "m0 module unavailable: %s", name);
    return NULL;
  }
  const char *source = "export const base = 40;";
  JSValue compiled = JS_Eval(context, source, strlen(source), name,
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(compiled)) return NULL;
  JSModuleDef *module = JS_VALUE_GET_PTR(compiled);
  JS_FreeValue(context, compiled);
  return module;
}

typedef struct {
  JSValue promise;
  JSValue reason;
} PromiseFailure;

/* Fixed fixtures can have at most one unhandled rejection at a time. */
static void rejection_tracker(JSContext *context, JSValueConst promise,
                              JSValueConst reason, JS_BOOL handled, void *opaque) {
  PromiseFailure *failure = opaque;
  if (handled && (JS_IsUndefined(failure->promise) ||
                  JS_VALUE_GET_PTR(failure->promise) != JS_VALUE_GET_PTR(promise))) return;
  JS_FreeValue(context, failure->promise);
  JS_FreeValue(context, failure->reason);
  failure->promise = handled ? JS_UNDEFINED : JS_DupValue(context, promise);
  failure->reason = handled ? JS_UNDEFINED : JS_DupValue(context, reason);
}

static JSValue throwing_job(JSContext *context, int argc, JSValueConst *argv) {
  (void)argc;
  (void)argv;
  return JS_ThrowInternalError(context, "m0-job-exception");
}

static JSValue module_export(JSContext *context, const char *mode, PromiseFailure *failure) {
  const char *source = !strcmp(mode, "--esm-dependency") ?
      "import { base } from 'm0:dependency'; export const answer = base + 2;" :
      "export const answer = 42;";
  if (!strcmp(mode, "--esm-missing")) source = "import { base } from 'm0:missing'; export const answer = base;";
  if (!strcmp(mode, "--esm-syntax")) source = "export const answer = ;";
  if (!strcmp(mode, "--esm-exception")) source = "throw new Error('m0-module-eval'); export const answer = 42;";
  if (!strcmp(mode, "--jobs-complete")) source = "export const completion = Promise.resolve(40).then(n => n + 2);";
  if (!strcmp(mode, "--jobs-exception")) source = "export const completion = Promise.resolve(42);";
  if (!strcmp(mode, "--jobs-rejection")) source = "export const completion = Promise.resolve(42); Promise.resolve().then(() => { throw new Error('m0-job-rejection'); });";
  if (!strcmp(mode, "--jobs-unfinished")) source = "export const completion = new Promise(() => {});";
  if (!strcmp(mode, "--jobs-budget")) source = "export const completion = new Promise(() => {}); function again() { Promise.resolve().then(again); } again();";
  JS_SetModuleLoaderFunc(JS_GetRuntime(context), NULL, fixture_loader, NULL);
  JSValue compiled = JS_Eval(context, source, strlen(source), "m0:main",
                             JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(compiled)) return compiled;
  JSModuleDef *module = JS_VALUE_GET_PTR(compiled);
  JSValue evaluated = JS_EvalFunction(context, compiled);
  if (JS_IsException(evaluated)) return evaluated;
  if (!strcmp(mode, "--jobs-exception") && JS_EnqueueJob(context, throwing_job, 0, NULL) < 0) {
    JS_FreeValue(context, evaluated);
    return JS_EXCEPTION;
  }
  JSContext *job_context;
  int executed = 0;
  unsigned jobs_run = 0;
  while (JS_IsJobPending(JS_GetRuntime(context))) {
    if (jobs_run++ == 256) {
      JS_FreeValue(context, evaluated);
      return JS_ThrowInternalError(context, "m0 job budget exceeded (256)");
    }
    executed = JS_ExecutePendingJob(JS_GetRuntime(context), &job_context);
    if (executed <= 0) break;
  }
  if (executed < 0) {
    JS_FreeValue(context, evaluated);
    return JS_Throw(context, JS_GetException(job_context));
  }
  if (!JS_IsUndefined(failure->reason)) {
    JS_FreeValue(context, evaluated);
    return JS_Throw(context, JS_DupValue(context, failure->reason));
  }
  if (JS_PromiseState(context, evaluated) == JS_PROMISE_REJECTED) {
    JSValue reason = JS_PromiseResult(context, evaluated);
    JS_FreeValue(context, evaluated);
    return JS_Throw(context, reason);
  }
  JS_FreeValue(context, evaluated);
  JSValue namespace = JS_GetModuleNamespace(context, module);
  if (JS_IsException(namespace)) return namespace;
  int jobs = !strncmp(mode, "--jobs-", 7);
  JSValue answer = JS_GetPropertyStr(context, namespace, jobs ? "completion" : "answer");
  JS_FreeValue(context, namespace);
  if (jobs && !JS_IsException(answer)) {
    JSPromiseStateEnum state = JS_PromiseState(context, answer);
    if (state == JS_PROMISE_PENDING) {
      JS_FreeValue(context, answer);
      return JS_ThrowInternalError(context, "m0 Promise unfinished with no pending jobs");
    }
    JSValue result = JS_PromiseResult(context, answer);
    JS_FreeValue(context, answer);
    return state == JS_PROMISE_REJECTED ? JS_Throw(context, result) : result;
  }
  return answer;
}

int main(int argc, char **argv) {
  if (argc == 2 && !strcmp(argv[1], "--facts")) {
#if defined(__x86_64__)
    const char *arch = "x86_64";
#elif defined(__aarch64__)
    const char *arch = "aarch64";
#else
    const char *arch = "unknown";
#endif
    printf("{\"probe\":\"quickjs-eval\",\"arch\":\"%s\",\"page_size\":%ld}\n",
           arch, sysconf(_SC_PAGESIZE));
    return 0;
  }
  if (!pointer_roundtrip()) {
    fputs("public JSValue pointer round-trip failed\n", stderr);
    return 3;
  }
  if (argc == 2 && !strcmp(argv[1], "--pointer-check")) {
    puts("JSValue pointer round-trip: pass");
    return 0;
  }
  if (argc > 2) return 2;
  const char *expression = argc == 2 ? argv[1] : "6 * 7";
  JSRuntime *runtime = JS_NewRuntime();
  if (!runtime) return 2;
  JSContext *context = JS_NewContext(runtime);
  if (!context) { JS_FreeRuntime(runtime); return 2; }
  PromiseFailure promise_failure = { JS_UNDEFINED, JS_UNDEFINED };
  JS_SetHostPromiseRejectionTracker(runtime, rejection_tracker, &promise_failure);
  JSValue value = !strncmp(expression, "--esm-", 6) || !strncmp(expression, "--jobs-", 7) ? module_export(context, expression, &promise_failure) :
      JS_Eval(context, expression, strlen(expression), "m0-eval", JS_EVAL_TYPE_GLOBAL);
  int failed = JS_IsException(value);
  if (failed) {
    JS_FreeValue(context, value);
    value = JS_GetException(context);
  }
  const char *text = JS_ToCString(context, value);
  if (!text) failed = 1;
  if (failed) fprintf(stderr, "eval exception: %s\n", text ? text : "conversion failed");
  else puts(text);
  JS_FreeCString(context, text);
  JS_FreeValue(context, value);
  JS_FreeValue(context, promise_failure.promise);
  JS_FreeValue(context, promise_failure.reason);
  JS_FreeContext(context);
  JS_FreeRuntime(runtime);
  return failed ? 1 : 0;
}
