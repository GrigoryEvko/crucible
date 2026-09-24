/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: fentry on sk_pacing_shift_update, tcp_pacing_check,
 * __msg_zerocopy_callback and sock_zerocopy_realloc, plus a sock_ops program for the
 * RTT callbacks. Neither pacing nor zerocopy ships tracepoints. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
