/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Attach points: the tracepoint/huge_memory collapse and khugepaged scan events, plus
 * fentry on __split_huge_pmd and __split_huge_pud. The split paths have no
 * tracepoints. */

#include "../common.h"

/* TODO: implement. */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
