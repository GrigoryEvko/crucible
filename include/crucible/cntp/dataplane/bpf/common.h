/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef __CRUCIBLE_CNTP_DATAPLANE_BPF_COMMON_H
#define __CRUCIBLE_CNTP_DATAPLANE_BPF_COMMON_H

#include "../../../perf/bpf/vmlinux.h"
#include <bpf/bpf_helpers.h>
#include "../../../perf/bpf/ring_ticket.h"

/*
 * write_idx counts each claim of a slot, and dropped counts the claims whose
 * write dropped. Neither resets. The header fills a whole cache line so that
 * the events array starts cache-aligned.
 */
struct timeline_header {
    __u64 write_idx;
    __u64 dropped;
    __u64 _pad[6];
};

#endif
