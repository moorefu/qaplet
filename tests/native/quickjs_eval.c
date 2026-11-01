/* SPDX-License-Identifier: MIT
 * M0 create/eval/destroy probe only; this is not a Runtime or SDK interface. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "quickjs.h"

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
  if (argc > 2) return 2;
  const char *expression = argc == 2 ? argv[1] : "6 * 7";
  JSRuntime *runtime = JS_NewRuntime();
  if (!runtime) return 2;
  JSContext *context = JS_NewContext(runtime);
  if (!context) { JS_FreeRuntime(runtime); return 2; }
  JSValue value = JS_Eval(context, expression, strlen(expression), "m0-eval", JS_EVAL_TYPE_GLOBAL);
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
  JS_FreeContext(context);
  JS_FreeRuntime(runtime);
  return failed ? 1 : 0;
}
