// SenseHub V2 userspace/BPF layout sentinel.
//
// This TU is intentionally compile-time heavy and runtime-light: it
// proves that the public userspace target sees the same
// CRUCIBLE_SENSE_HUB_EXTENDED mode that CMake selected for the BPF
// bytecode. The runtime body only exercises header-only counter delta
// semantics; it never loads BPF programs.

#include <crucible/perf/SenseHubV2.h>

#include <cstdint>
#include <cstdio>

namespace {

constexpr std::uint64_t expected_layout_hash(std::uint64_t counters,
                                             std::uint64_t gauges,
                                             std::uint64_t build_tag) noexcept {
    return (counters << 48) |
           (gauges << 32) |
           (std::uint64_t{crucible::perf::SENSE_HUB_VERSION} << 16) |
           build_tag;
}

static_assert(crucible::perf::SENSE_HUB_VERSION == 2);
static_assert(crucible::perf::SENSE_HUB_MAGIC == 0x4352424CU);
static_assert(sizeof(crucible::perf::sense_meta) == 64);

#if defined(CRUCIBLE_EXPECT_SENSE_HUB_EXTENDED) && \
    !defined(CRUCIBLE_SENSE_HUB_EXTENDED)
#  error "CMake enabled CRUCIBLE_SENSE_HUB_EXTENDED but userspace target did not receive the compile definition"
#endif

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
static_assert(crucible::perf::NUM_COUNTERS == 256);
static_assert(crucible::perf::NUM_GAUGES == 64);
static_assert(crucible::perf::BUILD_TAG == 0xDEB6);
static_assert(crucible::perf::SENSE_HUB_LAYOUT_HASH ==
              expected_layout_hash(256, 64, 0xDEB6));
static_assert(static_cast<std::uint32_t>(
                  crucible::perf::Idx::SKB_DROP_NEIGH_FAILED) == 128);
#else
static_assert(crucible::perf::NUM_COUNTERS == 128);
static_assert(crucible::perf::NUM_GAUGES == 32);
static_assert(crucible::perf::BUILD_TAG == 0xBA51);
static_assert(crucible::perf::SENSE_HUB_LAYOUT_HASH ==
              expected_layout_hash(128, 32, 0xBA51));
#endif

static_assert(sizeof(crucible::perf::CounterSnapshot) ==
              crucible::perf::NUM_COUNTERS * sizeof(std::uint64_t));
static_assert(sizeof(crucible::perf::GaugeSnapshot) ==
              crucible::perf::NUM_GAUGES * sizeof(std::uint64_t));
static_assert(static_cast<std::uint32_t>(
                  crucible::perf::Idx::MAP_FULL_DROPS) == 127);

}  // namespace

int main() {
    crucible::perf::CounterSnapshot before;
    crucible::perf::CounterSnapshot after;

    after.values[0] = 9;
    before.values[1] = 7;

    const crucible::perf::CounterDelta delta = after - before;
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
