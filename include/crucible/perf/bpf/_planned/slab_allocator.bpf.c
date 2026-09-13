/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: tracepoint/kmem/kmem_cache_alloc, kmem_cache_free, kfree and
 * kmalloc. The _node variants are merged into these base events, and only the SLUB
 * path remains from kernel 6.8. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
