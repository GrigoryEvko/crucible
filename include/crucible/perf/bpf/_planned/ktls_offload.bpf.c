/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: the tracepoint/tls device offload and resync events, plus
 * tracepoint/handshake/tls_alert_recv, tls_alert_send and tls_contenttype. The TLS
 * alert events live under the handshake subsystem, not under tls. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
