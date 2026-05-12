/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Canopy peer liveness probe stub.
 *
 * Future role: low-overhead passive liveness events for mesh peers.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/sock/inet_sock_set_state")
int crucible_canopy_peer_liveness_probe(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
