/* SPDX-License-Identifier: MIT */
#ifndef QAPLET_IO_INTERNAL_H
#define QAPLET_IO_INTERNAL_H
#include "runtime_internal.h"
#include <poll.h>
/* Owned pipe adapter. Child ends are returned to ProcessHost's existing
 * inheritance/rollback owner; parent ends belong exclusively to this module. */
qaplet_code qaplet_io_pipe(qaplet_runtime *,int,qaplet_io_endpoint *,int *,int64_t *,qaplet_process_status *);
void qaplet_stdio_classify(uint32_t,qaplet_stdio_info *,int *,int64_t *,uint32_t *);
int qaplet_io_standard_locked(qaplet_runtime *,uint32_t,qaplet_io_endpoint *,qaplet_stdio_info *);
int qaplet_io_ended_locked(qaplet_runtime *,const qaplet_io_endpoint *);
void qaplet_io_detach_locked(qaplet_runtime *,const qaplet_io_endpoint *);
void qaplet_io_shutdown(qaplet_runtime *,int closing);
void qaplet_io_collect(qaplet_runtime *);
unsigned qaplet_io_pump(qaplet_runtime *,uint64_t *);
int qaplet_io_ready_locked(qaplet_runtime *);
/* Native requests accepted from JS after this pump's issue phase must be
 * issued before sleeping. POSIX EAGAIN pipes still wait for fd readiness. */
int qaplet_io_issuable_locked(qaplet_runtime *);
int qaplet_io_referenced_locked(qaplet_runtime *);
unsigned qaplet_io_handoff(qaplet_runtime *,uint64_t);
/* Release JS handoffs after the complete Promise jobs checkpoint. No JS. */
void qaplet_io_js_checkpoint(qaplet_runtime *);
/* Acceptance mutex held. Stable scratch belongs to the owner drive. */
struct pollfd *qaplet_io_poll_locked(qaplet_runtime *,nfds_t *);
#endif
