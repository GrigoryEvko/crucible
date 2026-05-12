/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Canopy gossip fanout sample stub.
 *
 * Future role: publish mesh-gossip fanout evidence for HyParView/Plumtree
 * tuning. Current behavior: no-op tracepoint handler.
 */

#include "../../cntp/dataplane/bpf/net_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} canopy_gossip_fanout_events SEC(".maps");

SEC("tracepoint/net/net_dev_queue")
int crucible_canopy_gossip_fanout_sample(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
