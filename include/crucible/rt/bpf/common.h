/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Shared runtime-networking BPF include root.
 *
 * Runtime BPF stubs include this header rather than the perf helper header so
 * TC/XDP dataplane probes stay rooted in rt/. The vmlinux snapshot is still
 * shared from perf until a dedicated generated BTF bundle is promoted.
 */

#ifndef __CRUCIBLE_RT_BPF_COMMON_H
#define __CRUCIBLE_RT_BPF_COMMON_H

#include "../../perf/bpf/vmlinux.h"
#include <bpf/bpf_helpers.h>

struct timeline_header {
    __u64 write_idx;
};

#endif
