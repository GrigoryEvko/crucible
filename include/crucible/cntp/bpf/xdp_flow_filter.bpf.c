/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P XDP flow filter stub.
 *
 * Future role: earliest RX admission point for typed CNT-P flow keys.
 * Current behavior: pass every packet. No drop, redirect, or map mutation.
 */

#include "common.h"

#ifndef XDP_PASS
#define XDP_PASS 2
#endif

SEC("xdp")
int crucible_cntp_xdp_flow_filter(struct xdp_md *ctx)
{
    (void)ctx;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
