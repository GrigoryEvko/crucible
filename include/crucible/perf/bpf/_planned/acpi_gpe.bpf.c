/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: kprobe on acpi_ev_gpe_dispatch, acpi_ns_evaluate and acpi_irq.
 * The kernel exposes no acpi tracepoint subsystem, and the ACPICA signatures these
 * kprobes bind to carry no ABI guarantee across kernel versions. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
