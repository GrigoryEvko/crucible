/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: the tracepoint/vmscan kswapd wake and sleep events, wakeup_kswapd,
 * the LRU isolate and shrink events, and mm_shrink_slab_start and mm_shrink_slab_end.
 * The folio refactor removed mm_vmscan_writepage. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
