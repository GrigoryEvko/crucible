/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: tracepoint/preemptirq/preempt_disable, preempt_enable, irq_disable
 * and irq_enable. These events exist only when the kernel is built with
 * CONFIG_PREEMPTIRQ_TRACEPOINTS. Each carries a caller and a parent address. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
