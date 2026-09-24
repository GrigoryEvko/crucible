/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: tracepoint/filemap/mm_filemap_add_to_page_cache,
 * mm_filemap_delete_from_page_cache, mm_filemap_fault, mm_filemap_get_pages and
 * mm_filemap_map_pages. The payload carries an inode number, not a path. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
