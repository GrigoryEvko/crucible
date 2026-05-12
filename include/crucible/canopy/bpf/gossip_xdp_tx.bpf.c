/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Canopy gossip XDP_TX stub.
 *
 * Future role: XDP_TX replication for admitted mesh/gossip packets.
 * Current behavior: pass every packet; no replication.
 */

#include "common.h"

#ifndef XDP_PASS
#define XDP_PASS 2
#endif

SEC("xdp")
int crucible_canopy_gossip_xdp_tx(struct xdp_md *ctx)
{
    (void)ctx;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
