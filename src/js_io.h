/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_JS_IO_H
#define QAPLET_JS_IO_H
#include "runtime_internal.h"
/* A byte endpoint adapter over public C IO only. No ProcessHost layout. */
JSValue qaplet_js_io_endpoint(qaplet_runtime *,const qaplet_io_endpoint *,int writing);
#endif
