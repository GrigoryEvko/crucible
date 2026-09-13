/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach point: tracepoint/bpf_trace/bpf_trace_printk. It fires on every bpf_printk
 * call in every loaded program, and the payload truncates the format string. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
