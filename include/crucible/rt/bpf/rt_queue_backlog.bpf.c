/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Runtime queue backlog stub.
 *
 * Future role: publish queue pressure for rt backpressure/admission state.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/qdisc/qdisc_enqueue")
int crucible_rt_queue_backlog(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
