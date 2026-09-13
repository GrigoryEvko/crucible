/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: fentry and fexit on vfs_read and vfs_write, plus fentry on
 * do_filp_open. The return value is readable only on the fexit side. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
