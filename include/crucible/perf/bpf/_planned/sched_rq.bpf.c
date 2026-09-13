/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: fentry and fexit on enqueue_task_fair and dequeue_task_fair. The
 * fexit side is where the post-update run-queue depth is readable. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
