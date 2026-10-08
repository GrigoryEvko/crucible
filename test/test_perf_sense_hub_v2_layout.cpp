// The userspace target and the BPF bytecode must agree on
// CRUCIBLE_SENSE_HUB_EXTENDED.  This TU proves the userspace side sees the mode
// the build selected.  The runtime body exercises counter delta semantics only
// and never loads a BPF program.
//
// The TU also includes SenseHub.h.  The two hubs have different wire
// layouts, and each declares NUM_COUNTERS and Idx.  The second hub keeps
// its wire names in crucible::perf::v2, so the two headers compile in one
// translation unit and each name keeps its own value.

#include <crucible/perf/SenseHub.h>
#include <crucible/perf/SenseHubV2.h>

#include <cstdint>
#include <cstdio>
#include "test_assert.h"

namespace {

namespace v2 = crucible::perf::v2;

// The record sense_meta of include/crucible/perf/bpf/sense_hub_v2.bpf.c
// holds the magic at offset 0.  The format writes the magic as the four
// letters 'CRBL'.  A C compiler makes these letters into one 32-bit word,
// with 'C' as the most significant byte.
static_assert(v2::SENSE_HUB_MAGIC
              == ((std::uint32_t{'C'} << 24) | (std::uint32_t{'R'} << 16) | (std::uint32_t{'B'} << 8)
                  | std::uint32_t{'L'}));
static_assert(sizeof(v2::sense_meta) == 64);

#if defined(CRUCIBLE_EXPECT_SENSE_HUB_EXTENDED) && !defined(CRUCIBLE_SENSE_HUB_EXTENDED)
#error "CMake enabled CRUCIBLE_SENSE_HUB_EXTENDED but userspace target did not receive the compile definition"
#endif

// The record sense_meta holds the layout word at offset 16.  Bits 48 to 63
// hold the number of counters, and bits 32 to 47 hold the number of gauges.
// Bits 16 to 31 hold the version 2, and bits 0 to 15 hold the build tag.
// Each word here is the value that the BPF program calculates for its
// build.  A change of one constant of SenseHubV2.h fails this check.
#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
static_assert(v2::SENSE_HUB_LAYOUT_HASH == 0x0100'0040'0002'DEB6ULL,
              "The extended build has 256 counters, 64 gauges, version 2 and the build tag 0xDEB6.");
static_assert(static_cast<std::uint32_t>(v2::Idx::SKB_DROP_NEIGH_FAILED) == 128);
#else
static_assert(v2::SENSE_HUB_LAYOUT_HASH == 0x0080'0020'0002'BA51ULL,
              "The basic build has 128 counters, 32 gauges, version 2 and the build tag 0xBA51.");
#endif

static_assert(sizeof(v2::CounterSnapshot) == v2::NUM_COUNTERS * sizeof(std::uint64_t));
static_assert(sizeof(v2::GaugeSnapshot) == v2::NUM_GAUGES * sizeof(std::uint64_t));
static_assert(static_cast<std::uint32_t>(v2::Idx::MAP_FULL_DROPS) == 127);

// The first hub keeps its own layout in the same translation unit.
static_assert(crucible::perf::NUM_COUNTERS == 96);
static_assert(sizeof(crucible::perf::Snapshot) == 96 * sizeof(std::uint64_t));
static_assert(static_cast<std::uint32_t>(crucible::perf::MCE_COUNT) == 74);
static_assert(std::uint64_t{crucible::perf::NUM_COUNTERS} != std::uint64_t{v2::NUM_COUNTERS});

}  // namespace

int main() {
    v2::CounterSnapshot before;
    v2::CounterSnapshot after;

    after.values[0] = 9;
    before.values[1] = 7;

    const v2::CounterDelta delta = after - before;
    if (delta.deltas[0] != 9) {
        std::fprintf(stderr, "SenseHubV2 delta failed at slot 0\n");
        return 1;
    }
    if (delta.deltas[1] != 0) {
        std::fprintf(stderr, "SenseHubV2 delta did not saturate at slot 1\n");
        return 1;
    }

    crucible::test::pass("perf::SenseHubV2 layout smoke OK\n");
    return 0;
}
