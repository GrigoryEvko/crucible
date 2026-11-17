// The compile-time checks of crucible/cntp/dataplane/NetTimeline.h.  Each
// size and each offset is the one of the C struct of bpf/net_common.h, so the
// mirror reads the bytes that a program writes.

#include <crucible/cntp/dataplane/NetTimeline.h>

#include <cstddef>
#include <type_traits>

namespace crucible::cntp::dataplane {

static_assert(kNetTimelineCapacity == 4096u);
static_assert(kNetTimelineMask == 4095u);
static_assert((kNetTimelineCapacity & kNetTimelineMask) == 0u, "the capacity is a power of two");

static_assert(sizeof(NetEventKind) == 1);
static_assert(static_cast<std::uint8_t>(NetEventKind::PacingSample) == 1);
static_assert(static_cast<std::uint8_t>(NetEventKind::GossipFanout) == 10);

// struct crucible_net_flow_key: 4 + 4 + 2 + 2 + 1 + 3 bytes, aligned to 4.
static_assert(sizeof(NetFlowKey) == 16);
static_assert(alignof(NetFlowKey) == 4);
static_assert(offsetof(NetFlowKey, src_ip4) == 0);
static_assert(offsetof(NetFlowKey, dst_ip4) == 4);
static_assert(offsetof(NetFlowKey, src_port) == 8);
static_assert(offsetof(NetFlowKey, dst_port) == 10);
static_assert(offsetof(NetFlowKey, proto) == 12);
static_assert(offsetof(NetFlowKey, pad) == 13);
static_assert(std::is_trivially_copyable_v<NetFlowKey>);
static_assert(std::is_standard_layout_v<NetFlowKey>);
static_assert(std::has_unique_object_representations_v<NetFlowKey>);
static_assert(NetFlowKey{} == NetFlowKey{});
static_assert(!(NetFlowKey{.proto = 6} == NetFlowKey{.proto = 17}));

// struct crucible_net_event: 8 + 8 + 4 + 2 + 1 + 1 + 8 bytes, aligned to 8.
static_assert(sizeof(NetEvent) == 32);
static_assert(alignof(NetEvent) == 8);
static_assert(offsetof(NetEvent, value0) == 0);
static_assert(offsetof(NetEvent, value1) == 8);
static_assert(offsetof(NetEvent, ifindex) == 16);
static_assert(offsetof(NetEvent, queue_id) == 20);
static_assert(offsetof(NetEvent, kind) == 22);
static_assert(offsetof(NetEvent, pad) == 23);
static_assert(offsetof(NetEvent, ts_ns) == 24);
static_assert(std::is_trivially_copyable_v<NetEvent>);
static_assert(std::has_unique_object_representations_v<NetEvent>);

// struct timeline_header and struct crucible_net_timeline.
static_assert(sizeof(NetTimelineHeader) == 8);
static_assert(offsetof(NetTimelineHeader, write_idx) == 0);
static_assert(sizeof(NetTimeline) == 8 + kNetTimelineCapacity * sizeof(NetEvent));
static_assert(alignof(NetTimeline) == 8);
static_assert(offsetof(NetTimeline, hdr) == 0);
static_assert(offsetof(NetTimeline, events) == 8);
static_assert(std::is_trivially_copyable_v<NetTimeline>);

static_assert(net_event_kind_name(NetEventKind::LldpRx) == "LldpRx");

}  // namespace crucible::cntp::dataplane
