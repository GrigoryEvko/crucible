/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P dataplane TC egress classifier stub.
 *
 * Future role: enforce source-tagged dataplane TcEbpf flow class decisions at
 * clsact/egress. Current behavior: TC_ACT_OK for every skb.
 */

#include "net_common.h"

#ifndef TC_ACT_OK
#define TC_ACT_OK 0
#endif

struct crucible_tc_flow_class {
    __u32 classid;
    __u8 dscp;
    __u8 priority;
    __u8 action;
    __u8 _pad;
};

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 65536);
    __type(key, struct crucible_net_flow_key);
    __type(value, struct crucible_tc_flow_class);
} dp_tc_flow_classes SEC(".maps");

SEC("tc")
int crucible_dp_tc_egress_classify(struct __sk_buff *skb)
{
    (void)skb;
    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
