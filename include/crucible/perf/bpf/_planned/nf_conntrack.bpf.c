/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: fentry on __nf_conntrack_alloc, nf_conntrack_destroy, nf_ct_delete
 * and nf_conntrack_in. Conntrack ships no tracepoints, so there is no tracepoint
 * alternative to these function hooks. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
