/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * CNT-P sock_ops telemetry stub.
 *
 * Future role: per-socket RTT, retransmit, state, and pacing observations.
 * Current behavior: observe no callbacks and publish no maps.
 */

#include "common.h"

SEC("sockops")
int crucible_cntp_sock_ops_telemetry(struct bpf_sock_ops *ops)
{
    (void)ops;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
