/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P fq pacing sample stub.
 *
 * Future role: publish per-skb pacing evidence for BBR-family policy and fq
 * qdisc validation. Current behavior: TC_ACT_OK for every skb.
 */

#include "../../rt/bpf/net_common.h"

#ifndef TC_ACT_OK
#define TC_ACT_OK 0
#endif

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} cntp_pacing_events SEC(".maps");

SEC("tc")
int crucible_cntp_fq_pacing_sample(struct __sk_buff *skb)
{
    (void)skb;
    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
