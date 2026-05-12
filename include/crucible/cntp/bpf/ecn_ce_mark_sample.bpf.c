/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P ECN-CE mark sample stub.
 *
 * Future role: feed ECN congestion evidence into CNT-P control loops before
 * userspace TCP_INFO polling catches up. Current behavior: XDP_PASS.
 */

#include "../../rt/bpf/net_common.h"

#ifndef XDP_PASS
#define XDP_PASS 2
#endif

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} cntp_ecn_events SEC(".maps");

SEC("xdp")
int crucible_cntp_ecn_ce_mark_sample(struct xdp_md *ctx)
{
    (void)ctx;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
