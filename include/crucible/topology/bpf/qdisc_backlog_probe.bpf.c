/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

SEC("tracepoint/qdisc/qdisc_dequeue")
int crucible_topology_qdisc_backlog_probe(void* ctx) {
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
