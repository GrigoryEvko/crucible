#pragma once

// The userspace mirrors of the records of bpf/net_common.h.  A BPF program of
// the dataplane writes these records into a map, and userspace reads the same
// bytes through an mmap of the map.  Each mirror has the size, the alignment
// and the field offsets of its C struct, and each mirror keeps the names of
// the C fields.  The check file of this header holds the layout of each one.
//
// A timeline has 131080 bytes, so a caller reads it through the mmap of its
// map and does not make one on a stack.

#include <fixy/Core.h>

#include <cstdint>

namespace crucible::cntp::dataplane {

// CRUCIBLE_NET_TIMELINE_CAPACITY.  The count is a power of two, so the slot of
// a write index is the index under kNetTimelineMask.
inline constexpr std::uint32_t kNetTimelineCapacity = 4096;
inline constexpr std::uint64_t kNetTimelineMask = kNetTimelineCapacity - 1u;

// enum crucible_net_event_kind.  The kind field of an event holds the value
// in one byte.
enum class NetEventKind : std::uint8_t {
    PacingSample = 1,
    EcnCeSample = 2,
    LossSample = 3,
    XdpTimestamp = 4,
    SocketBudget = 5,
    AckClock = 6,
    BbrModel = 7,
    FqBudget = 8,
    LldpRx = 9,
    GossipFanout = 10,
};

[[nodiscard]] constexpr ::fixy::TextView net_event_kind_name(NetEventKind kind) noexcept {
    return ::foundation::core::enum_name_text(kind);
}

// struct crucible_net_flow_key: the key of the flow maps of the TC programs.
// Its 16 bytes have no padding that the compiler adds, because pad names the
// three last bytes.  The equality reads each byte, pad too, so two equal keys
// have the same bytes and the same hash in a map image.
struct NetFlowKey {
    std::uint32_t src_ip4 = 0;
    std::uint32_t dst_ip4 = 0;
    std::uint16_t src_port = 0;
    std::uint16_t dst_port = 0;
    std::uint8_t proto = 0;
    ::fixy::FixedArray<std::uint8_t, 3> pad{};

    [[nodiscard]] friend constexpr bool operator==(NetFlowKey const& left, NetFlowKey const& right) noexcept {
        return left.src_ip4 == right.src_ip4 && left.dst_ip4 == right.dst_ip4 && left.src_port == right.src_port
            && left.dst_port == right.dst_port && left.proto == right.proto && left.pad == right.pad;
    }
};

// struct crucible_net_event: one sample of a timeline.  The publish of
// bpf/net_common.h writes ts_ns last, after a full barrier, so ts_ns marks
// the end of the write of a slot.
struct NetEvent {
    std::uint64_t value0 = 0;
    std::uint64_t value1 = 0;
    std::uint32_t ifindex = 0;
    std::uint16_t queue_id = 0;
    NetEventKind kind = NetEventKind::PacingSample;
    std::uint8_t pad = 0;
    std::uint64_t ts_ns = 0;
};

// struct timeline_header of bpf/common.h.  The program takes each slot with a
// fetch-add on write_idx.
struct NetTimelineHeader {
    std::uint64_t write_idx = 0;
};

// struct crucible_net_timeline: the value of one timeline map.  The slot of
// the write index i is events at i & kNetTimelineMask.
struct NetTimeline {
    NetTimelineHeader hdr{};
    ::fixy::FixedArray<NetEvent, kNetTimelineCapacity> events{};
};

}  // namespace crucible::cntp::dataplane
