/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Shared Warden BPF include root.
 *
 * Warden BPF stubs include this header rather than the perf helper header so
 * enforcement probes stay rooted in warden/. The vmlinux snapshot is still
 * shared from perf until a dedicated generated BTF bundle is promoted.
 */

#ifndef __CRUCIBLE_WARDEN_BPF_COMMON_H
#define __CRUCIBLE_WARDEN_BPF_COMMON_H

#include "../../perf/bpf/vmlinux.h"
#include <bpf/bpf_helpers.h>

struct timeline_header {
    __u64 write_idx;
};

#endif
