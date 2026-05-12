/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Topology qdisc backlog probe stub.
 *
 * Future role: per-interface qdisc enqueue/dequeue/drop telemetry.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/qdisc/qdisc_dequeue")
int crucible_topology_qdisc_backlog_probe(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
