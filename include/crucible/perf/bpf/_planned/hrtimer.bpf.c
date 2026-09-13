/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: tracepoint/timer/hrtimer_setup, hrtimer_start, hrtimer_expire_entry,
 * hrtimer_expire_exit and hrtimer_cancel. Kernels before 6.15 spell hrtimer_setup
 * as hrtimer_init. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
