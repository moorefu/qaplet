/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_SDK_TEMP_H
#define QAPLET_SDK_TEMP_H
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/* The native runner supplies its own temporary directory. Never require a
 * pre-existing global /tmp on Windows; do not create a missing Host root. */
static int sdk_temporary_file(char *path,size_t capacity,const char *prefix){
  const char *root=getenv("TMPDIR");
  if(!root || !*root)root=getenv("TEMP");
  if(!root || !*root)root=getenv("TMP");
  if(!root || !*root)root="/tmp";
  int length=snprintf(path,capacity,"%s/%s-XXXXXX",root,prefix);
  if(length<0 || (size_t)length>=capacity){errno=ENAMETOOLONG;return -1;}
  return mkstemp(path);
}
#endif
