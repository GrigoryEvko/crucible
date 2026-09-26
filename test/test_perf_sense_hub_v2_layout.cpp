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

namespace {

namespace v2 = crucible::perf::v2;

constexpr std::uint64_t expected_layout_hash(std::uint64_t counters, std::uint64_t gauges,
                                             std::uint64_t build_tag) noexcept {
    return (counters << 48) | (gauges << 32) | (std::uint64_t{v2::SENSE_HUB_VERSION} << 16) | build_tag;
}

static_assert(v2::SENSE_HUB_VERSION == 2);
static_assert(v2::SENSE_HUB_MAGIC == 0x4352424CU);
static_assert(sizeof(v2::sense_meta) == 64);

#if defined(CRUCIBLE_EXPECT_SENSE_HUB_EXTENDED) && !defined(CRUCIBLE_SENSE_HUB_EXTENDED)
#error "CMake enabled CRUCIBLE_SENSE_HUB_EXTENDED but userspace target did not receive the compile definition"
#endif

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
static_assert(v2::NUM_COUNTERS == 256);
static_assert(v2::NUM_GAUGES == 64);
static_assert(v2::BUILD_TAG == 0xDEB6);
static_assert(v2::SENSE_HUB_LAYOUT_HASH == expected_layout_hash(256, 64, 0xDEB6));
static_assert(static_cast<std::uint32_t>(v2::Idx::SKB_DROP_NEIGH_FAILED) == 128);
#else
static_assert(v2::NUM_COUNTERS == 128);
static_assert(v2::NUM_GAUGES == 32);
static_assert(v2::BUILD_TAG == 0xBA51);
static_assert(v2::SENSE_HUB_LAYOUT_HASH == expected_layout_hash(128, 32, 0xBA51));
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

    std::printf("perf::SenseHubV2 layout smoke OK\n");
    return 0;
}
