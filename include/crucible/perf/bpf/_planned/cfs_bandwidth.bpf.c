/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: fentry and fexit on throttle_cfs_rq and unthrottle_cfs_rq. The
 * kernel ships no CFS throttle tracepoint, and both functions are static, so
 * attachment depends on BTF and on the symbol being present at load. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
