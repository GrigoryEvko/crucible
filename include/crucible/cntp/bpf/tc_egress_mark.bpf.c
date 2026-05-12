/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P TC egress marking stub.
 *
 * Future role: apply typed DSCP/ECN/classid decisions for admitted flows.
 * Current behavior: TC_ACT_OK for every skb.
 */

#include "common.h"

#ifndef TC_ACT_OK
#define TC_ACT_OK 0
#endif

SEC("tc")
int crucible_cntp_tc_egress_mark(struct __sk_buff *skb)
{
    (void)skb;
    return TC_ACT_OK;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
