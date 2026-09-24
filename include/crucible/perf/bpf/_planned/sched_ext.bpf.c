/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach: a struct_ops program implementing struct sched_ext_ops. The kernel calls
 * these callbacks in place of the fair-class scheduler for the tasks handed to this
 * scheduler, so the program owns dispatch rather than observing it. */

#include "../common.h"
#include <bpf/bpf_helpers.h>

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
