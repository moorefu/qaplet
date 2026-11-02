/* SPDX-License-Identifier: MIT */
/* Public-header-only Host: mutable fixtures are process-local, never SDK files. */
#include "qaplet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "public module policy failed at line %d: %s\n", __LINE__, #c); return 1; } } while (0)

static int write_source(const char *directory, const char *name, const char *source) {
  char path[4096];
  CHECK(snprintf(path, sizeof(path), "%s/%s", directory, name) < sizeof(path));
  FILE *file = fopen(path, "wb");
  CHECK(file && fwrite(source, 1, strlen(source), file) == strlen(source));
  CHECK(!fclose(file));
  return 0;
}

static qaplet_code finish(qaplet_runtime *runtime, qaplet_progress *progress, qaplet_code code) {
  unsigned count = 0;
  while (!code && progress->phase == QAPLET_PHASE_PENDING && count++ < 16)
    code = qaplet_pump(runtime, progress, NULL);
  return progress->phase == QAPLET_PHASE_DONE ? code : QAPLET_BAD_STATE;
}

static qaplet_code entry(qaplet_runtime *runtime, const char *directory, const char *name) {
  char path[4096];
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  if (snprintf(path, sizeof(path), "%s/%s", directory, name) >= sizeof(path)) return QAPLET_INVALID_ARGUMENT;
  qaplet_code code = qaplet_run_entry(runtime, path, strlen(path), &progress, NULL);
  return finish(runtime, &progress, code);
}

static int number(qaplet_runtime *runtime, const char *source, const char *base, double expected) {
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  qaplet_code code = base ? qaplet_eval_with_base(runtime, source, strlen(source), base, strlen(base), &progress, NULL) :
      qaplet_eval(runtime, source, strlen(source), &progress, NULL);
  CHECK(finish(runtime, &progress, code) == QAPLET_OK);
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_DOUBLE && result.number == expected);
  return 0;
}

static int release(qaplet_runtime *runtime) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_close(runtime, NULL) == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && !snapshot.module_count && !snapshot.source_cache_bytes && !snapshot.engine_current_bytes);
  CHECK(qaplet_destroy(runtime, NULL) == QAPLET_OK);
  return 0;
}

int sdk_module_policy(const char *mode) {
  char temporary[] = "/tmp/qaplet-module-policy-XXXXXX";
  char *directory = mkdtemp(temporary);
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *runtime = NULL;
  CHECK(directory);
  CHECK(!write_source(directory, "valid.js", "globalThis.validRuns=(globalThis.validRuns||0)+1; globalThis.baseUrl=import.meta.url; export const answer=42;"));
  CHECK(qaplet_create(&config, &runtime, NULL) == QAPLET_OK);
  CHECK(entry(runtime, directory, "valid.js") == QAPLET_OK);
  qaplet_progress progress = QAPLET_PROGRESS_INIT;
  qaplet_result result = QAPLET_RESULT_INIT;
  char base[4096];
  CHECK(qaplet_eval(runtime, "baseUrl", 7, &progress, NULL) == QAPLET_OK);
  result.buffer = base; result.capacity = sizeof(base) - 1;
  CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.type == QAPLET_VALUE_STRING);
  base[result.length] = 0;
  if (!strcmp(mode, "--module-nul")) {
    CHECK(!number(runtime, "import('./valid.js'+String.fromCharCode(0)+'suffix').then(()=>1,()=>42)", base, 42));
    CHECK(!write_source(directory, "static.js", "import './valid.js\\0suffix';"));
    CHECK(entry(runtime, directory, "static.js") == QAPLET_JS_EXCEPTION);
    CHECK(!write_source(directory, "unicode.js", "import './valid.js\\u0000suffix';"));
    CHECK(entry(runtime, directory, "unicode.js") == QAPLET_JS_EXCEPTION);
    CHECK(!write_source(directory, "valid", "export const answer=42;"));
    CHECK(!number(runtime, "import('./valid'+String.fromCharCode(0)+'.js').then(()=>1,()=>42)", base, 42));
    CHECK(!number(runtime, "validRuns", NULL, 1));
    CHECK(qaplet_eval(runtime, "'a\\0b'", 6, &progress, NULL) == QAPLET_OK);
    unsigned char bytes[3];
    result.buffer = bytes; result.capacity = sizeof(bytes);
    CHECK(qaplet_get_result(runtime, &result, NULL) == QAPLET_OK && result.length == 3 && !memcmp(bytes, "a\0b", 3));
    CHECK(!release(runtime));
    const char *files[] = {"valid.js", "static.js", "unicode.js", "valid"};
    for (unsigned i = 0; i < sizeof(files) / sizeof(*files); ++i) {
      char path[4096]; snprintf(path, sizeof(path), "%s/%s", directory, files[i]); CHECK(!unlink(path));
    }
    CHECK(!rmdir(directory));
    puts("module NUL: computed/static names rejected; existing files intact; ordinary NUL strings preserved: pass");
    return 0;
  }
  return 2;
}
