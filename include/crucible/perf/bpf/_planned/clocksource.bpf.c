/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: kprobe on clocksource_select, clocksource_watchdog_work and
 * __clocksource_change_rating, plus tracepoint/timer/tick_stop. The kernel exposes
 * no clocksource tracepoint subsystem. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
