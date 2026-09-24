/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

SEC("tracepoint/napi/napi_poll")
int crucible_topology_napi_pressure_probe(void* ctx) {
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
