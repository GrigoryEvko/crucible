/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Runtime pacing-budget stub.
 *
 * Future role: enforce rt-owned send budget decisions at TC egress. Current
 * behavior: TC_ACT_OK for every skb; CNT-P remains the policy owner.
 */

#include "net_common.h"

#ifndef TC_ACT_OK
#define TC_ACT_OK 0
#endif

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 65536);
    __type(key, struct crucible_net_flow_key);
    __type(value, __u64);
} rt_pacing_budget_bps SEC(".maps");

SEC("tc")
int crucible_rt_pacing_budget(struct __sk_buff *skb)
{
    (void)skb;
    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
