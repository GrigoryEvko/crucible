/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: tracepoint/tcp/tcp_destroy_sock, tcp_rcv_space_adjust,
 * tcp_retransmit_skb, tcp_retransmit_synack, tcp_receive_reset, tcp_send_reset and
 * tcp_rcvbuf_grow. tcp_retransmit_skb covers tail loss probe, timeout and fast
 * retransmit alike. The kernel exposes no separate tail-loss-probe event. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
