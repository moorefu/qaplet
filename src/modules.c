/* SPDX-License-Identifier: MIT */
#define _COSMO_SOURCE 1
#include "runtime_internal.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "libc/dce.h"

struct qaplet_module {
  char *url, *source;
  size_t source_length;
  JSValue compiled, failure;
  qaplet_code code;
  int main;
  qaplet_module *next;
};
struct qaplet_module_alias {
  char *url;
  qaplet_module *module;
  qaplet_module_alias *next;
};

/* Registry/identity storage is SDK storage. Retained source bytes have their
 * separate cache ledger. Every retained engine value is an owned reference. */
static void *native_alloc(qaplet_runtime *runtime, size_t size) {
  pthread_mutex_lock(&runtime->mutex);
  void *value = NULL;
  if (size <= runtime->snapshot.limits.native_storage_bytes - runtime->snapshot.native_current_bytes)
    value = calloc(1, size);
  if (value) {
    runtime->snapshot.native_current_bytes += size;
    if (runtime->snapshot.native_current_bytes > runtime->snapshot.native_peak_bytes)
      runtime->snapshot.native_peak_bytes = runtime->snapshot.native_current_bytes;
  } else {
    qaplet_record_termination_locked(runtime, QAPLET_MEMORY_LIMIT);
  }
  pthread_mutex_unlock(&runtime->mutex);
  return value;
}

static void native_free(qaplet_runtime *runtime, void *value, size_t size) {
  if (!value) return;
  free(value);
  pthread_mutex_lock(&runtime->mutex);
  runtime->snapshot.native_current_bytes -= size;
  pthread_mutex_unlock(&runtime->mutex);
}

void *qaplet_native_alloc(qaplet_runtime *runtime, size_t size) { return native_alloc(runtime, size); }
void qaplet_native_free(qaplet_runtime *runtime, void *value, size_t size) { native_free(runtime, value, size); }

static char *native_string(qaplet_runtime *runtime, const char *value) {
  size_t size = strlen(value) + 1;
  char *copy = native_alloc(runtime, size);
  if (copy) memcpy(copy, value, size);
  return copy;
}

static JSValue module_throw(qaplet_runtime *runtime, qaplet_code code) {
  qaplet_code termination = qaplet_execution_check(runtime);
  if (termination) code = termination;
  JS_ThrowTypeError(runtime->context, "%s", code == QAPLET_MODULE_RESOLVE ? "Module resolution failed" :
      code == QAPLET_MODULE_LIMIT ? "Module count limit" : code == QAPLET_SOURCE_TOO_LARGE ?
      "Module source limit" : code == QAPLET_UNSUPPORTED ? "Unsupported module request" : "Module loading failed");
  JSValue error = JS_GetException(runtime->context);
  JS_FreeValue(runtime->context, runtime->module_exception);
  runtime->module_exception = JS_DupValue(runtime->context, error);
  runtime->module_exception_code = code;
  return JS_Throw(runtime->context, error);
}

qaplet_code qaplet_module_exception_code(qaplet_runtime *runtime, JSValueConst error) {
  if (!JS_IsUndefined(runtime->module_exception) && JS_SameValue(runtime->context, runtime->module_exception, error))
    return runtime->module_exception_code;
  for (qaplet_module *entry = runtime->modules; entry; entry = entry->next)
    if (entry->code && JS_SameValue(runtime->context, entry->failure, error)) return entry->code;
  return QAPLET_JS_EXCEPTION;
}

static int hex(unsigned char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  return -1;
}

/* Only local absolute file URLs are accepted. Encoded separators/NUL and URL
 * components are rejected before handing the native path to the filesystem. */
static char *url_path(qaplet_runtime *runtime, const char *url) {
  if (strncmp(url, "file:///", 8) || url[8] == '/' || strchr(url, '?') || strchr(url, '#')) return NULL;
  size_t length = strlen(url + 7);
  char *path = native_alloc(runtime, length + 1);
  if (!path) return NULL;
  size_t output = 0;
  for (size_t i = 7; url[i]; ++i) {
    unsigned char value = url[i];
    if (value == '%') {
      int a = hex(url[i + 1]), b = url[i + 1] ? hex(url[i + 2]) : -1;
      if (a < 0 || b < 0) goto invalid;
      value = a * 16 + b;
      i += 2;
      if (!value || value == '/' || value == '\\') goto invalid;
    }
    if (value == '\\') goto invalid;
    path[output++] = value;
  }
  path[output] = 0;
  if (!qaplet_utf8_valid(path, output)) goto invalid;
  /* Cosmopolitan's native Windows drive syntax is /C/path. */
  if (IsWindows() && output >= 4 && path[0] == '/' && isalpha((unsigned char)path[1]) &&
      path[2] == ':' && path[3] == '/') {
    path[1] = toupper((unsigned char)path[1]);
    memmove(path + 2, path + 3, output - 2);
  }
  char *copy = native_string(runtime, path);
  native_free(runtime, path, length + 1);
  return copy;
invalid:
  native_free(runtime, path, length + 1);
  return NULL;
}

static char *path_url(qaplet_runtime *runtime, const char *path) {
  size_t length = strlen(path);
  if (length > (SIZE_MAX - 10) / 3) return NULL;
  size_t capacity = length * 3 + 10;
  char *url = native_alloc(runtime, capacity);
  if (!url) return NULL;
  memcpy(url, "file://", 7);
  size_t output = 7;
  const char digits[] = "0123456789ABCDEF";
  for (size_t i = 0; i < length; ++i) {
    unsigned char value = path[i];
    if ((value < 128 && isalnum(value)) || value == '/' || value == '-' || value == '.' || value == '_' || value == '~')
      url[output++] = value;
    else {
      url[output++] = '%'; url[output++] = digits[value >> 4]; url[output++] = digits[value & 15];
    }
    if (IsWindows() && i == 1 && path[0] == '/' && isalpha(value) && path[2] == '/') {
      url[output - 1] = toupper(value);
      url[output++] = ':';
    }
  }
  url[output] = 0;
  char *copy = native_string(runtime, url);
  native_free(runtime, url, capacity);
  return copy;
}

/* Preserve dot components for realpath: collapsing link/../ lexically would
 * change filesystem meaning. Successful request spellings are frozen aliases. */
static char *absolute_path(qaplet_runtime *runtime, const char *base, const char *name) {
  size_t base_length = strlen(base), name_length = strlen(name);
  if (base_length > SIZE_MAX - name_length - 2) return NULL;
  size_t capacity = base_length + name_length + 2;
  char *joined = native_alloc(runtime, capacity);
  if (!joined) return NULL;
  if (name[0] == '/') memcpy(joined, name, name_length + 1);
  else snprintf(joined, capacity, "%s/%s", base, name);
  if (IsWindows())
    for (size_t i = 0; joined[i]; ++i) if (joined[i] == '\\') joined[i] = '/';
  char *copy = native_string(runtime, joined);
  native_free(runtime, joined, capacity);
  return copy;
}

static int supported_path(const char *path) {
  const char *name = strrchr(path, '/');
  const char *dot = strrchr(name ? name + 1 : path, '.');
  if (!dot) return 1;
  return IsWindows() ? !strcasecmp(dot, ".js") || !strcasecmp(dot, ".mjs") :
      !strcmp(dot, ".js") || !strcmp(dot, ".mjs");
}

static qaplet_module *find_module(qaplet_runtime *runtime, const char *url) {
  for (qaplet_module *entry = runtime->modules; entry; entry = entry->next)
    if (!strcmp(entry->url, url)) return entry;
  return NULL;
}

static qaplet_module *resolve(qaplet_runtime *runtime, const char *base, const char *name, int root) {
  char *path = NULL, *directory = NULL;
  qaplet_code code = QAPLET_MODULE_RESOLVE;
  /* Registered host and declared builtin names are not file URLs. */
  if (!root && (!strncmp(name, "host:", 5) || !strcmp(name, "qaplet:events") || !strcmp(name, "qaplet:runtime"))) {
    qaplet_module *entry = find_module(runtime, name);
    if (entry) return entry;
    module_throw(runtime, QAPLET_MODULE_RESOLVE);
    return NULL;
  }
  /* Refuse URL components and encoded separators before looking for native
   * files which may coincidentally have those literal spellings. */
  if (strchr(name, '?') || strchr(name, '#')) { module_throw(runtime, code); return NULL; }
  for (size_t i = 0; name[i]; ++i) {
    if (name[i] == '%' && name[i + 1] && name[i + 2]) {
      int a = hex(name[i + 1]), b = hex(name[i + 2]);
      int decoded = a >= 0 && b >= 0 ? a * 16 + b : -1;
      if (decoded == 0 || decoded == '/' || decoded == '\\') {
        module_throw(runtime, code); return NULL;
      }
    }
  }
  if (!strncmp(name, "file:", 5)) path = url_path(runtime, name);
  else if (root) {
    if (IsWindows() && isalpha((unsigned char)name[0]) && name[1] == ':' && (name[2] == '/' || name[2] == '\\')) {
      char *native = native_string(runtime, name);
      if (native) {
        native[0] = '/'; native[1] = toupper((unsigned char)name[0]);
        path = absolute_path(runtime, runtime->initial_cwd, native);
        native_free(runtime, native, strlen(name) + 1);
      }
    } else if (!strchr(name, ':') && strncmp(name, "//", 2) && strncmp(name, "\\\\", 2))
      path = absolute_path(runtime, runtime->initial_cwd, name);
    else if (strchr(name, ':') && strncmp(name, "//", 2) && strncmp(name, "\\\\", 2)) code = QAPLET_UNSUPPORTED;
  } else if (!strncmp(name, "./", 2) || !strncmp(name, "../", 3)) {
    directory = url_path(runtime, base);
    if (directory) {
      char *slash = strrchr(directory, '/');
      if (slash) *slash = 0;
      path = absolute_path(runtime, directory, name);
      if (slash) *slash = '/';
    }
  } else code = QAPLET_UNSUPPORTED;
  if (directory) native_free(runtime, directory, strlen(directory) + 1);
  if (!path) { module_throw(runtime, code); return NULL; }
  if (!supported_path(path)) { code = QAPLET_UNSUPPORTED; goto failed; }
  char *alias_url = path_url(runtime, path);
  if (!alias_url) goto failed;
  qaplet_module *canonical = find_module(runtime, alias_url);
  if (canonical) {
    native_free(runtime, alias_url, strlen(alias_url) + 1);
    native_free(runtime, path, strlen(path) + 1);
    if (canonical->code && !root) { JS_Throw(runtime->context, JS_DupValue(runtime->context, canonical->failure)); return NULL; }
    return canonical;
  }
  for (qaplet_module_alias *alias = runtime->module_aliases; alias; alias = alias->next) {
    if (!strcmp(alias->url, alias_url)) {
      qaplet_module *entry = alias->module;
      native_free(runtime, alias_url, strlen(alias_url) + 1);
      native_free(runtime, path, strlen(path) + 1);
      if (entry->code && !root) { JS_Throw(runtime->context, JS_DupValue(runtime->context, entry->failure)); return NULL; }
      return entry;
    }
  }
  char *real = realpath(path, NULL);
  native_free(runtime, path, strlen(path) + 1);
  path = NULL;
  if (!real) { native_free(runtime, alias_url, strlen(alias_url) + 1); goto failed; }
  if (!supported_path(real)) {
    free(real); native_free(runtime, alias_url, strlen(alias_url) + 1);
    code = QAPLET_UNSUPPORTED; goto failed;
  }
  char *url = path_url(runtime, real);
  free(real);
  if (!url) { native_free(runtime, alias_url, strlen(alias_url) + 1); goto failed; }
  qaplet_module *entry = find_module(runtime, url);
  if (!entry) {
    if (runtime->snapshot.module_count >= runtime->snapshot.limits.module_count) {
      code = QAPLET_MODULE_LIMIT;
      native_free(runtime, url, strlen(url) + 1);
      native_free(runtime, alias_url, strlen(alias_url) + 1);
      goto failed;
    }
    entry = native_alloc(runtime, sizeof(*entry));
    if (!entry) { native_free(runtime, url, strlen(url) + 1); native_free(runtime, alias_url, strlen(alias_url) + 1); goto failed; }
    entry->url = url;
    entry->compiled = entry->failure = JS_UNDEFINED;
    entry->main = root;
    entry->next = runtime->modules;
    runtime->modules = entry;
    pthread_mutex_lock(&runtime->mutex);
    ++runtime->snapshot.module_count;
    pthread_mutex_unlock(&runtime->mutex);
  } else native_free(runtime, url, strlen(url) + 1);
  qaplet_module_alias *alias = native_alloc(runtime, sizeof(*alias));
  if (!alias) { native_free(runtime, alias_url, strlen(alias_url) + 1); goto failed; }
  alias->url = alias_url; alias->module = entry;
  alias->next = runtime->module_aliases; runtime->module_aliases = alias;
  if (entry->code && !root) { JS_Throw(runtime->context, JS_DupValue(runtime->context, entry->failure)); return NULL; }
  return entry;
failed:
  if (path) native_free(runtime, path, strlen(path) + 1);
  module_throw(runtime, code);
  return NULL;
}

static void save_failure(qaplet_runtime *runtime, qaplet_module *entry, JSValueConst exception) {
  entry->code = qaplet_module_exception_code(runtime, exception);
  JS_FreeValue(runtime->context, entry->failure);
  entry->failure = JS_DupValue(runtime->context, exception);
}

static int compile_module(qaplet_runtime *runtime, qaplet_module *entry) {
  if (entry->code) { JS_Throw(runtime->context, JS_DupValue(runtime->context, entry->failure)); return -1; }
  if (!JS_IsUndefined(entry->compiled)) return 0;
  if (!strncmp(entry->url, "host:", 5) || !strncmp(entry->url, "qaplet:", 7)) {
    JSModuleDef *module = !strcmp(entry->url, "qaplet:events") ?
        qaplet_events_load(runtime) : !strcmp(entry->url, "qaplet:runtime") ?
        qaplet_runtime_load(runtime) : qaplet_host_load(runtime, entry->url);
    if (!module) goto failure;
    entry->compiled = JS_DupValue(runtime->context, JS_MKPTR(JS_TAG_MODULE, module));
    return 0;
  }
  char *path = url_path(runtime, entry->url);
  if (!path) { module_throw(runtime, QAPLET_MODULE_LOAD); goto failure; }
  int fd = open(path, O_RDONLY | O_NONBLOCK);
  native_free(runtime, path, strlen(path) + 1);
  struct stat status;
  if (fd < 0) { module_throw(runtime, QAPLET_MODULE_LOAD); goto failure; }
  if (fstat(fd, &status) || !S_ISREG(status.st_mode) || status.st_size < 0) {
    close(fd); module_throw(runtime, QAPLET_MODULE_LOAD); goto failure;
  }
  FILE *file = fdopen(fd, "rb");
  if (!file) { close(fd); module_throw(runtime, QAPLET_MODULE_LOAD); goto failure; }
  uint64_t length = status.st_size;
  if (length > runtime->snapshot.limits.source_bytes || length >
      runtime->snapshot.limits.source_cache_bytes - runtime->snapshot.source_cache_bytes) {
    fclose(file); module_throw(runtime, QAPLET_SOURCE_TOO_LARGE); goto failure;
  }
  char *source = malloc((size_t)length + 1);
  if (!source) {
    fclose(file);
    pthread_mutex_lock(&runtime->mutex);
    qaplet_record_termination_locked(runtime, QAPLET_MEMORY_LIMIT);
    pthread_mutex_unlock(&runtime->mutex);
    module_throw(runtime, QAPLET_MEMORY_LIMIT); goto failure;
  }
  size_t actual = fread(source, 1, length, file);
  int extra = fgetc(file), bad = ferror(file);
  fclose(file);
  if (actual != length || extra != EOF || bad) { free(source); module_throw(runtime, QAPLET_MODULE_LOAD); goto failure; }
  source[length] = 0;
  entry->source = source; entry->source_length = length;
  pthread_mutex_lock(&runtime->mutex);
  runtime->snapshot.source_cache_bytes += length;
  pthread_mutex_unlock(&runtime->mutex);
  if (!qaplet_utf8_valid(source, length)) { module_throw(runtime, QAPLET_INVALID_UTF8); goto failure; }
  JSValue value = JS_Eval(runtime->context, source, length, entry->url, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(value)) goto failure;
  entry->compiled = value;
  JSValue meta = JS_GetImportMeta(runtime->context, JS_VALUE_GET_PTR(value));
  if (JS_IsException(meta)) goto failure;
  int result = JS_DefinePropertyValueStr(runtime->context, meta, "url", JS_NewString(runtime->context, entry->url), JS_PROP_C_W_E);
  if (result >= 0) result = JS_DefinePropertyValueStr(runtime->context, meta, "main", JS_NewBool(runtime->context, entry->main), JS_PROP_C_W_E);
  JS_FreeValue(runtime->context, meta);
  if (result < 0) goto failure;
  return 0;
failure:;
  JSValue exception = JS_GetException(runtime->context);
  save_failure(runtime, entry, exception);
  JS_Throw(runtime->context, exception);
  return -1;
}

static char *normalize_module(JSContext *context, const char *base, const char *name, void *opaque) {
  qaplet_runtime *runtime = opaque;
  qaplet_module *entry = resolve(runtime, base, name, 0);
  return entry ? js_strdup(context, entry->url) : NULL;
}

static JSModuleDef *load_module(JSContext *context, const char *name, void *opaque, JSValueConst attributes) {
  (void)context; (void)attributes;
  qaplet_runtime *runtime = opaque;
  qaplet_module *entry = find_module(runtime, name);
  if (!entry || compile_module(runtime, entry)) return NULL;
  return JS_VALUE_GET_PTR(entry->compiled);
}

static int check_attributes(JSContext *context, void *opaque, JSValueConst attributes) {
  (void)context; (void)attributes;
  module_throw(opaque, QAPLET_UNSUPPORTED);
  return -1;
}

qaplet_code qaplet_check_runtime_config(const qaplet_config *config) {
  if ((!config->argv && config->argc) || config->argc > UINT32_MAX ||
      config->argc > SIZE_MAX / sizeof(qaplet_string) || (!config->cwd && config->cwd_length))
    return QAPLET_INVALID_ARGUMENT;
  for (size_t i = 0; i < config->argc; ++i) {
    const qaplet_string *argument = &config->argv[i];
    if (!argument->data && argument->length) return QAPLET_INVALID_ARGUMENT;
    if (argument->length > QAPLET_VALUE_BYTES_MAX) return QAPLET_VALUE_TOO_LARGE;
    if (!qaplet_utf8_valid(argument->data, argument->length)) return QAPLET_INVALID_UTF8;
  }
  if (config->cwd) {
    if (!config->cwd_length || config->cwd_length > QAPLET_VALUE_BYTES_MAX)
      return QAPLET_INVALID_ARGUMENT;
    if (memchr(config->cwd, 0, config->cwd_length) || (config->cwd_length > 1 &&
        config->cwd[0] == '/' && config->cwd[1] == '/'))
      return QAPLET_INVALID_ARGUMENT;
    if (!qaplet_utf8_valid(config->cwd, config->cwd_length)) return QAPLET_INVALID_UTF8;
    if (config->cwd[0] != '/' && !(IsWindows() && config->cwd_length >= 3 &&
        isalpha((unsigned char)config->cwd[0]) && config->cwd[1] == ':' &&
        (config->cwd[2] == '/' || config->cwd[2] == '\\'))) return QAPLET_INVALID_ARGUMENT;
  }
  return QAPLET_OK;
}

void qaplet_runtime_snapshot_dispose(qaplet_runtime *runtime) {
  for (size_t i = 0; i < runtime->initial_argc; ++i)
    native_free(runtime, (void *)runtime->initial_argv[i].data, runtime->initial_argv[i].length + 1);
  native_free(runtime, runtime->initial_argv, runtime->initial_argc * sizeof(qaplet_string));
  if (runtime->initial_cwd) native_free(runtime, runtime->initial_cwd, strlen(runtime->initial_cwd) + 1);
  runtime->initial_cwd = NULL; runtime->initial_argv = NULL; runtime->initial_argc = 0;
}

qaplet_code qaplet_modules_init(qaplet_runtime *runtime, const qaplet_config *config) {
  runtime->module_exception = JS_UNDEFINED;
  char *requested = NULL;
  if (config->cwd) {
    requested = native_alloc(runtime, config->cwd_length + 1);
    if (!requested) return QAPLET_MEMORY_LIMIT;
    memcpy(requested, config->cwd, config->cwd_length);
    requested[config->cwd_length] = 0;
  }
  char *cwd = requested ? realpath(requested, NULL) : getcwd(NULL, 0);
  native_free(runtime, requested, config->cwd_length + 1);
  if (!cwd) return QAPLET_MODULE_RESOLVE;
  struct stat status;
  if (!qaplet_utf8_valid(cwd, strlen(cwd))) { free(cwd); return QAPLET_INVALID_UTF8; }
  if (stat(cwd, &status) || !S_ISDIR(status.st_mode)) { free(cwd); return QAPLET_MODULE_RESOLVE; }
  runtime->initial_cwd = native_string(runtime, cwd);
  free(cwd);
  if (!runtime->initial_cwd) return QAPLET_MEMORY_LIMIT;
  if (config->argc) {
    runtime->initial_argv = native_alloc(runtime, config->argc * sizeof(qaplet_string));
    if (!runtime->initial_argv) return QAPLET_MEMORY_LIMIT;
    runtime->initial_argc = config->argc;
    for (size_t i = 0; i < config->argc; ++i) {
      size_t length = config->argv[i].length;
      char *copy = native_alloc(runtime, length + 1);
      if (!copy) return QAPLET_MEMORY_LIMIT;
      if (length) memcpy(copy, config->argv[i].data, length);
      runtime->initial_argv[i] = (qaplet_string){copy, length};
    }
  }
  JS_SetModuleLoaderFunc2(runtime->engine, normalize_module, load_module, check_attributes, runtime);
  qaplet_code code = qaplet_modules_register_host(runtime, "qaplet:events");
  return code ? code : qaplet_modules_register_host(runtime, "qaplet:runtime");
}

static int initialize_runtime(JSContext *context, JSModuleDef *module) {
  qaplet_runtime *runtime = JS_GetContextOpaque(context);
  JSValue argv = JS_NewArray(context);
  if (JS_IsException(argv)) return -1;
  for (size_t i = 0; i < runtime->initial_argc; ++i) {
    JSValue string = JS_NewStringLen(context, runtime->initial_argv[i].data, runtime->initial_argv[i].length);
    if (JS_IsException(string) || JS_DefinePropertyValueUint32(context, argv, i, string, JS_PROP_ENUMERABLE) < 0) {
      JS_FreeValue(context, argv); return -1;
    }
  }
  if (JS_DefinePropertyValueStr(context, argv, "length", JS_NewUint32(context, runtime->initial_argc), 0) < 0 ||
      JS_PreventExtensions(context, argv) < 0) { JS_FreeValue(context, argv); return -1; }
  if (JS_SetModuleExport(context, module, "argv", argv) < 0) return -1;
  return JS_SetModuleExport(context, module, "cwd", JS_NewString(context, runtime->initial_cwd));
}

JSModuleDef *qaplet_runtime_load(qaplet_runtime *runtime) {
  JSModuleDef *module = JS_NewCModule(runtime->context, "qaplet:runtime", initialize_runtime);
  if (module && (JS_AddModuleExport(runtime->context, module, "argv") < 0 ||
                 JS_AddModuleExport(runtime->context, module, "cwd") < 0)) return NULL;
  return module;
}

qaplet_code qaplet_modules_register_host(qaplet_runtime *runtime, const char *name) {
  if (find_module(runtime, name)) return QAPLET_INVALID_ARGUMENT;
  if (runtime->snapshot.module_count >= runtime->snapshot.limits.module_count) return QAPLET_MODULE_LIMIT;
  qaplet_module *entry = native_alloc(runtime, sizeof(*entry));
  if (!entry) return QAPLET_MEMORY_LIMIT;
  entry->url = native_string(runtime, name);
  if (!entry->url) { native_free(runtime, entry, sizeof(*entry)); return QAPLET_MEMORY_LIMIT; }
  entry->compiled = entry->failure = JS_UNDEFINED;
  entry->next = runtime->modules;
  runtime->modules = entry;
  pthread_mutex_lock(&runtime->mutex);
  ++runtime->snapshot.module_count;
  pthread_mutex_unlock(&runtime->mutex);
  return QAPLET_OK;
}

void qaplet_modules_dispose(qaplet_runtime *runtime) {
  JS_FreeValue(runtime->context, runtime->module_exception);
  runtime->module_exception = JS_UNDEFINED;
  while (runtime->modules) {
    qaplet_module *entry = runtime->modules;
    runtime->modules = entry->next;
    JS_FreeValue(runtime->context, entry->compiled);
    JS_FreeValue(runtime->context, entry->failure);
    free(entry->source);
    native_free(runtime, entry->url, strlen(entry->url) + 1);
    native_free(runtime, entry, sizeof(*entry));
  }
  while (runtime->module_aliases) {
    qaplet_module_alias *alias = runtime->module_aliases;
    runtime->module_aliases = alias->next;
    native_free(runtime, alias->url, strlen(alias->url) + 1);
    native_free(runtime, alias, sizeof(*alias));
  }
  /* The immutable factory cwd snapshot belongs to the handle, like the limits.
   * Keep it in the CLOSED native baseline and release it with destroy. */
  pthread_mutex_lock(&runtime->mutex);
  runtime->snapshot.module_count = runtime->snapshot.source_cache_bytes = 0;
  pthread_mutex_unlock(&runtime->mutex);
}

void qaplet_modules_complete(qaplet_runtime *runtime) {
  if (runtime->active_module && !JS_IsUndefined(runtime->entry_promise) &&
      JS_PromiseState(runtime->context, runtime->entry_promise) == JS_PROMISE_REJECTED) {
    JSValue error = JS_PromiseResult(runtime->context, runtime->entry_promise);
    save_failure(runtime, runtime->active_module, error);
    JS_FreeValue(runtime->context, error);
  }
  runtime->active_module = NULL;
}

qaplet_code qaplet_run_entry(qaplet_runtime *runtime, const char *path, size_t length,
                            qaplet_progress *progress, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(progress, sizeof(*progress));
  if (!code && (!path || !length || length == SIZE_MAX || memchr(path, 0, length))) code = QAPLET_INVALID_ARGUMENT;
  if (!code && !qaplet_utf8_valid(path, length)) code = QAPLET_INVALID_UTF8;
  if (!code) code = qaplet_owner(runtime);
  if (!code && runtime->snapshot.lifecycle != QAPLET_OPEN) code = QAPLET_BAD_STATE;
  if (!code && runtime->snapshot.phase == QAPLET_PHASE_PENDING) code = QAPLET_BUSY;
  if (code) return qaplet_report(diagnostic, code);
  char *copy = native_alloc(runtime, length + 1);
  if (!copy) return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  memcpy(copy, path, length);
  qaplet_module *entry = resolve(runtime, NULL, copy, 1);
  native_free(runtime, copy, length + 1);
  if (!entry) {
    JSValue error = JS_GetException(runtime->context);
    code = qaplet_module_exception_code(runtime, error);
    JS_FreeValue(runtime->context, error);
    return qaplet_report(diagnostic, code);
  }
  /* A root's source-size refusal is discoverable before accepting execution.
   * Preserve the previous result while freezing the known identity's failure. */
  if (!entry->code && !entry->source) {
    char *native = url_path(runtime, entry->url);
    struct stat status;
    if (native && !stat(native, &status) && S_ISREG(status.st_mode) && status.st_size >= 0 &&
        ((uint64_t)status.st_size > runtime->snapshot.limits.source_bytes ||
         (uint64_t)status.st_size > runtime->snapshot.limits.source_cache_bytes - runtime->snapshot.source_cache_bytes)) {
      module_throw(runtime, QAPLET_SOURCE_TOO_LARGE);
      JSValue error = JS_GetException(runtime->context);
      save_failure(runtime, entry, error);
      JS_FreeValue(runtime->context, error);
      native_free(runtime, native, strlen(native) + 1);
      return qaplet_report(diagnostic, QAPLET_SOURCE_TOO_LARGE);
    }
    if (native) native_free(runtime, native, strlen(native) + 1);
  }
  code = qaplet_execution_begin(runtime, NULL);
  if (code) return qaplet_report(diagnostic, code);
  runtime->module_execution = 1;
  runtime->active_module = entry;
  JSValue value = JS_UNDEFINED;
  if (compile_module(runtime, entry)) value = JS_EXCEPTION;
  else if (!(code = qaplet_execution_check(runtime)))
    value = JS_EvalFunction(runtime->context, JS_DupValue(runtime->context, entry->compiled));
  qaplet_code termination = qaplet_execution_check(runtime);
  if (termination) code = termination;
  if (JS_IsException(value)) {
    JSValue error = JS_GetException(runtime->context);
    if (!code) {
      save_failure(runtime, entry, error);
      qaplet_capture_exception(runtime, error);
      code = entry->code;
    }
    JS_FreeValue(runtime->context, error);
  } else if (!code && (int)JS_PromiseState(runtime->context, value) >= 0)
    runtime->entry_promise = JS_DupValue(runtime->context, value);
  JS_FreeValue(runtime->context, value);
  runtime->execution_code = code;
  code = qaplet_execution_checkpoint(runtime, progress, 1);
  return qaplet_report_result(runtime, diagnostic, code);
}

qaplet_code qaplet_eval_with_base(qaplet_runtime *runtime, const char *source, size_t length,
                                 const char *base, size_t base_length,
                                 qaplet_progress *progress, qaplet_diagnostic *diagnostic) {
  qaplet_code code = qaplet_check_diagnostic(diagnostic);
  if (code) return code;
  code = qaplet_check_structure(progress, sizeof(*progress));
  if (!code && (!base || !base_length || base_length == SIZE_MAX || memchr(base, 0, base_length))) code = QAPLET_INVALID_ARGUMENT;
  if (!code && (!source && length)) code = QAPLET_INVALID_ARGUMENT;
  if (!code && (!qaplet_utf8_valid(base, base_length) || !qaplet_utf8_valid(source, length))) code = QAPLET_INVALID_UTF8;
  if (!code) code = qaplet_owner(runtime);
  if (!code && runtime->snapshot.lifecycle != QAPLET_OPEN) code = QAPLET_BAD_STATE;
  if (!code && runtime->snapshot.phase == QAPLET_PHASE_PENDING) code = QAPLET_BUSY;
  if (!code && length > runtime->snapshot.limits.source_bytes) code = QAPLET_SOURCE_TOO_LARGE;
  if (code) return qaplet_report(diagnostic, code);
  char *copy = native_alloc(runtime, base_length + 1);
  if (!copy) return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  memcpy(copy, base, base_length);
  char *path = url_path(runtime, copy);
  native_free(runtime, copy, base_length + 1);
  if (!path) {
    code = qaplet_execution_check(runtime);
    return qaplet_report(diagnostic, code ? code : QAPLET_MODULE_RESOLVE);
  }
  char *absolute = absolute_path(runtime, "/", path);
  native_free(runtime, path, strlen(path) + 1);
  char *url = absolute ? path_url(runtime, absolute) : NULL;
  if (absolute) native_free(runtime, absolute, strlen(absolute) + 1);
  if (!url) return qaplet_report(diagnostic, QAPLET_MEMORY_LIMIT);
  code = qaplet_eval_filename(runtime, source, length, url, progress, diagnostic);
  native_free(runtime, url, strlen(url) + 1);
  return code;
}
