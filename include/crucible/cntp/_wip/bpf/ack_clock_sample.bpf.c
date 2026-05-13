/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P WIP ACK-clock sample stub.
 *
 * Future role: sample ACK timing for pacing-rate and inflight estimates.
 * Current behavior: no-op tracepoint handler.
 */

#include "../../dataplane/bpf/net_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} cntp_wip_ack_clock_events SEC(".maps");

SEC("tracepoint/tcp/tcp_probe")
int crucible_cntp_wip_ack_clock_sample(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
