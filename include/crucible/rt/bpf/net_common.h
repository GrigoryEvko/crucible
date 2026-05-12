/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Shared runtime-networking BPF wire contracts.
 *
 * These structs are intentionally POD and fixed-width. Userspace facades must
 * mirror them exactly before any domain BPF program is promoted from stub to
 * loaded production bytecode.
 */

#ifndef __CRUCIBLE_RT_BPF_NET_COMMON_H
#define __CRUCIBLE_RT_BPF_NET_COMMON_H

#include "common.h"

#ifndef BPF_F_MMAPABLE
#define BPF_F_MMAPABLE (1U << 10)
#endif

#define CRUCIBLE_NET_TIMELINE_CAPACITY 4096
#define CRUCIBLE_NET_TIMELINE_MASK (CRUCIBLE_NET_TIMELINE_CAPACITY - 1)

enum crucible_net_event_kind {
    CRUCIBLE_NET_EVENT_PACING_SAMPLE = 1,
    CRUCIBLE_NET_EVENT_ECN_CE_SAMPLE = 2,
    CRUCIBLE_NET_EVENT_LOSS_SAMPLE = 3,
    CRUCIBLE_NET_EVENT_XDP_TIMESTAMP = 4,
    CRUCIBLE_NET_EVENT_SOCKET_BUDGET = 5,
};

struct crucible_net_flow_key {
    __u32 src_ip4;
    __u32 dst_ip4;
    __u16 src_port;
    __u16 dst_port;
    __u8  proto;
    __u8  _pad[3];
};

struct crucible_net_event {
    __u64 value0;
    __u64 value1;
    __u32 ifindex;
    __u16 queue_id;
    __u8  kind;
    __u8  _pad;
    __u64 ts_ns;
};

struct crucible_net_timeline {
    struct timeline_header hdr;
    struct crucible_net_event events[CRUCIBLE_NET_TIMELINE_CAPACITY];
};

static __always_inline void
crucible_net_publish_event(struct crucible_net_timeline *timeline,
                           struct crucible_net_event event)
{
    __u64 idx = __sync_fetch_and_add(&timeline->hdr.write_idx, 1);
    struct crucible_net_event *slot =
        &timeline->events[idx & CRUCIBLE_NET_TIMELINE_MASK];

    slot->value0 = event.value0;
    slot->value1 = event.value1;
    slot->ifindex = event.ifindex;
    slot->queue_id = event.queue_id;
    slot->kind = event.kind;
    slot->_pad = 0;
    __sync_synchronize();
    slot->ts_ns = event.ts_ns;
}

#endif
