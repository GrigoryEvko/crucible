/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Topology XDP RX timestamp probe stub.
 *
 * Future role: capture NIC RX timestamp availability and path jitter evidence
 * for PTP/pingmesh policy. Current behavior: XDP_PASS.
 */

#include "../../cntp/dataplane/bpf/net_common.h"

#ifndef XDP_PASS
#define XDP_PASS 2
#endif

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} topology_rx_timestamp_events SEC(".maps");

SEC("xdp")
int crucible_topology_xdp_rx_timestamp_probe(struct xdp_md *ctx)
{
    (void)ctx;
    return XDP_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
