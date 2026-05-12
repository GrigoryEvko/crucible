/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Cog NIC probe stub.
 *
 * Future role: bind low-level NIC evidence to Cog identity.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/net/net_dev_queue")
int crucible_cog_nic_probe(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
