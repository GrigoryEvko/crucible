/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

SEC("tracepoint/net/net_dev_start_xmit")
int crucible_cog_offload_cap_probe(void* ctx) {
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
