/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

#ifndef SK_PASS
#define SK_PASS 1
#endif

SEC("sk_msg")
int crucible_cntp_sk_msg_credit_gate(struct sk_msg_md* msg) {
    (void)msg;
    return SK_PASS;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
