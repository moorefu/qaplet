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
  return code ? code : progress->phase == QAPLET_PHASE_DONE ? QAPLET_OK : QAPLET_BAD_STATE;
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


static int remove_fixture(const char *directory, const char *name) {
  char path[4096];
  CHECK(snprintf(path, sizeof(path), "%s/%s", directory, name) < sizeof(path));
  CHECK(!unlink(path));
  return 0;
}

static int frozen_failures(qaplet_runtime *runtime, const char *directory, const char *base) {
  qaplet_snapshot snapshot = QAPLET_SNAPSHOT_INIT;
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK);
  uint64_t original_bytes = snapshot.source_cache_bytes;
  CHECK(!write_source(directory, "valid.js", "globalThis.validRuns=99; export const answer=99;"));
  CHECK(entry(runtime, directory, "valid.js") == QAPLET_OK);
  CHECK(!number(runtime, "validRuns", NULL, 1));
  CHECK(!remove_fixture(directory, "valid.js"));
  CHECK(entry(runtime, directory, "valid.js") == QAPLET_OK);
  CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == 1 && snapshot.source_cache_bytes == original_bytes);
  CHECK(!write_source(directory, "valid.js", "globalThis.validRuns=99; export const answer=99;"));
  const char *names[] = {"parse.js", "throw.js", "tla.js", "missing-graph.js"};
  const char *bad[] = {"export const = 0;", "globalThis.partial=(globalThis.partial||0)+1; throw new Error('frozen');", "globalThis.tlaPartial=(globalThis.tlaPartial||0)+1; await Promise.reject(new Error('frozen TLA'));", "import './absent.js';"};
  const qaplet_code codes[] = {QAPLET_JS_EXCEPTION, QAPLET_JS_EXCEPTION, QAPLET_JS_EXCEPTION, QAPLET_MODULE_RESOLVE};
  for (unsigned i = 0; i < 4; ++i) {
    CHECK(!write_source(directory, names[i], bad[i]));
    CHECK(entry(runtime, directory, names[i]) == codes[i]);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == i+2 && snapshot.lifecycle == QAPLET_OPEN);
    uint64_t bytes = snapshot.source_cache_bytes;
    CHECK(!write_source(directory, names[i], "globalThis.repaired=(globalThis.repaired||0)+1; export const answer=42;"));
    CHECK(entry(runtime, directory, names[i]) == codes[i]);
    CHECK(qaplet_get_snapshot(runtime, &snapshot, NULL) == QAPLET_OK && snapshot.module_count == i+2 && snapshot.source_cache_bytes == bytes);
  }
  CHECK(!number(runtime, "partial===1 && tlaPartial===1 && validRuns===1 && typeof repaired==='undefined' ? 42:0", NULL, 42));
  CHECK(!write_source(directory, "link-dep.js", "export const other=1;"));
  CHECK(!write_source(directory, "link.js", "import {answer} from './link-dep.js'; globalThis.linkAnswer=answer;"));
  CHECK(!number(runtime, "import('./link.js').then(()=>0,()=>42)", base, 42));
  CHECK(!write_source(directory, "link-dep.js", "export const answer=42;"));
  CHECK(!number(runtime, "import('./link.js').then(()=>0,()=>42)", base, 42));
  CHECK(entry(runtime, directory, "link.js") == QAPLET_JS_EXCEPTION);
  CHECK(!number(runtime, "typeof linkAnswer==='undefined' && partial===1 && validRuns===1 ? 42:0", NULL, 42));
  qaplet_config config = QAPLET_CONFIG_INIT;
  qaplet_runtime *fresh = NULL;
  CHECK(qaplet_create(&config, &fresh, NULL) == QAPLET_OK);
  CHECK(entry(fresh, directory, "valid.js") == QAPLET_OK);
  CHECK(!number(fresh, "validRuns", NULL, 99));
  for (unsigned i = 0; i < 4; ++i) CHECK(entry(fresh, directory, names[i]) == QAPLET_OK);
  CHECK(!number(fresh, "repaired", NULL, 4));
  CHECK(entry(fresh, directory, "link.js") == QAPLET_OK);
  CHECK(!number(fresh, "linkAnswer", NULL, 42));
  CHECK(!release(fresh));
  /* A realpath-normalized non-file is a retained LOAD failure, with no source charge. */
  char path[4096]; snprintf(path,sizeof(path),"%s/nonfile.js",directory);
  CHECK(!mkdir(path,0700));
  CHECK(entry(runtime,directory,"nonfile.js")==QAPLET_MODULE_LOAD);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK);
  uint64_t count=snapshot.module_count, bytes=snapshot.source_cache_bytes;
  CHECK(!rmdir(path)); CHECK(!write_source(directory,"nonfile.js","globalThis.nonfileRepaired=42;"));
  CHECK(entry(runtime,directory,"nonfile.js")==QAPLET_MODULE_LOAD);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.module_count==count && snapshot.source_cache_bytes==bytes);
  CHECK(qaplet_create(&config,&fresh,NULL)==QAPLET_OK);
  CHECK(entry(fresh,directory,"nonfile.js")==QAPLET_OK && !number(fresh,"nonfileRepaired",NULL,42));
  CHECK(!release(fresh));
  /* Unresolvable requests consume no identity; a later file can be discovered. */
  CHECK(entry(runtime,directory,"later.js")==QAPLET_MODULE_RESOLVE);
  CHECK(qaplet_get_snapshot(runtime,&snapshot,NULL)==QAPLET_OK && snapshot.module_count==count);
  CHECK(!write_source(directory,"later.js","globalThis.laterAnswer=42;"));
  CHECK(entry(runtime,directory,"later.js")==QAPLET_OK && !number(runtime,"laterAnswer",NULL,42));
  CHECK(!release(runtime));
  for(unsigned i=0;i<4;++i) CHECK(!remove_fixture(directory,names[i]));
  const char *extra[]={"valid.js","link-dep.js","link.js","nonfile.js","later.js"};
  for(unsigned i=0;i<sizeof(extra)/sizeof(*extra);++i) CHECK(!remove_fixture(directory,extra[i]));
  CHECK(!rmdir(directory));
  puts("module cache: source/load/parse/link/eval/TLA failures frozen; fresh repair; partial effects retained; accounting: pass");
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
  if (!strcmp(mode, "--module-cache")) return frozen_failures(runtime, directory, base);
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
