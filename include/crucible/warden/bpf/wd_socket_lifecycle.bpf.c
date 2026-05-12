/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Warden socket lifecycle stub.
 *
 * Future role: publish socket connect/close lifecycle facts into Warden state.
 * Current behavior: no-op tracepoint handler.
 */

#include "common.h"

SEC("tracepoint/sock/inet_sock_set_state")
int crucible_wd_socket_lifecycle(void *ctx)
{
    (void)ctx;
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
