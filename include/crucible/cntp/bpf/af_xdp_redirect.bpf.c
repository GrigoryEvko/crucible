/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P AF_XDP redirect stub.
 *
 * Future role: redirect selected admitted flows into zero-copy AF_XDP queues.
 * Current behavior: pass every packet through the kernel stack.
 */

#include "common.h"

#ifndef XDP_PASS
#define XDP_PASS 2
#endif

SEC("xdp")
int crucible_cntp_af_xdp_redirect(struct xdp_md *ctx)
{
    (void)ctx;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
