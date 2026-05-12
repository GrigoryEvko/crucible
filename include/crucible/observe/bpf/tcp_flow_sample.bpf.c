/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Observe TCP flow sample stub.
 *
 * Future role: passive flow samples for observation without owning policy.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/tcp/tcp_probe")
int crucible_observe_tcp_flow_sample(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
