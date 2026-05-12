/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Topology LLDP RX sample stub.
 *
 * Future role: reserve the XDP-side packet tap for LLDP adjacency evidence.
 * Current behavior: pass every packet through the kernel stack.
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
} topology_lldp_rx_events SEC(".maps");

SEC("xdp")
int crucible_topology_lldp_rx_sample(struct xdp_md *ctx)
{
    (void)ctx;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
