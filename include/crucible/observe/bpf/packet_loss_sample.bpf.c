/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Observe packet loss sample stub.
 *
 * Future role: sample TCP retransmit/loss events for observation streams.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/tcp/tcp_retransmit_skb")
int crucible_observe_packet_loss_sample(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
