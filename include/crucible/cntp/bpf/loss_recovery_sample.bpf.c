/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P loss-recovery sample stub.
 *
 * Future role: feed fast loss/retransmit evidence into CNT-P recovery and
 * path-swap policy. Current behavior: no-op tracepoint handler.
 */

#include "../../rt/bpf/net_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} cntp_loss_recovery_events SEC(".maps");

SEC("tracepoint/tcp/tcp_retransmit_skb")
int crucible_cntp_loss_recovery_sample(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
