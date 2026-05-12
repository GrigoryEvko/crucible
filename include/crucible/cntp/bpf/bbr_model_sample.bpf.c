/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P BBR-family model sample stub.
 *
 * Future role: collect low-latency socket model evidence for userspace
 * BBRv3-shaped control loops. Current behavior: observe no callbacks.
 */

#include "../dataplane/bpf/net_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} cntp_bbr_model_events SEC(".maps");

SEC("sockops")
int crucible_cntp_bbr_model_sample(struct bpf_sock_ops *ops)
{
    (void)ops;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
