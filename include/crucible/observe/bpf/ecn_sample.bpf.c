/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Observe ECN sample stub.
 *
 * Future role: passive ECN sample stream for dashboards and offline policy
 * evaluation. Current behavior: no-op tracepoint handler.
 */

#include "../../rt/bpf/net_common.h"

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __uint(map_flags, BPF_F_MMAPABLE);
    __type(key, __u32);
    __type(value, struct crucible_net_timeline);
} observe_ecn_events SEC(".maps");

SEC("tracepoint/tcp/tcp_probe")
int crucible_observe_ecn_sample(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
