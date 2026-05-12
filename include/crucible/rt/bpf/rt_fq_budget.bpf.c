/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Runtime fq-budget stub.
 *
 * Future role: publish fq/qdisc send-budget evidence for rt admission and
 * CNT-P pacing policy. Current behavior: TC_ACT_OK for every skb.
 */

#include "net_common.h"

#ifndef TC_ACT_OK
#define TC_ACT_OK 0
#endif

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} rt_fq_budget_events SEC(".maps");

SEC("tc")
int crucible_rt_fq_budget(struct __sk_buff *skb)
{
    (void)skb;
    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
