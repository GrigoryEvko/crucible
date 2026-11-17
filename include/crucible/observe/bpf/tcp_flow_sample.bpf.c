/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

SEC("tracepoint/tcp/tcp_probe")
int crucible_observe_tcp_flow_sample(void* ctx) {
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
