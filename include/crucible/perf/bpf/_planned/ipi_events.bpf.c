/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: tracepoint/ipi/ipi_send_cpu and ipi_send_cpumask on every
 * architecture. The handler-side events ipi_raise, ipi_entry and ipi_exit exist only
 * on arm64. The x86 equivalent is a raw tracepoint on the irq_vectors events. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
