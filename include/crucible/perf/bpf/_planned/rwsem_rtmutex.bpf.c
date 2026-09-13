/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: kprobe and kretprobe on down_read, down_write, up_read, up_write,
 * rt_mutex_lock and rt_mutex_unlock, plus tracepoint/lock/contention_begin and
 * contention_end for the lock variant. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
