/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: the tracepoint/oom reaper events, tracepoint/memcg per-cgroup
 * counter events, and tracepoint/ras/memory_failure_event. Hardware memory errors
 * live under the ras subsystem. There is no memory_failure subsystem. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
