#pragma once

// The shared parts of the tests of fixy/concurrent/ParallelTree.h: the
// context of the threads, the allocation capability, the floats of probe E4,
// the compare of bits, and the counts of the runs.
//
// Each test runs a form with 1, 2, 4, 8 and 64 shards.  The coupling
// WaitOnEachOther gives each shard its own thread, so those runs use 1, 2, 4,
// 8 and 64 threads.  The coupling Independent takes the threads of the
// parallelism rule, which starts at most 16.  Each result must have the bits
// of the form of one thread of foundation/core.

#include <fixy/Core.h>
#include <foundation/effects/Effect.h>

#include "../foundation/philox_stream.h"

#include <cstddef>
#include <cstdint>

namespace crucible::test::parallel_tree {

namespace eff = ::foundation::effects;

using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

[[nodiscard]] inline ::foundation::core::Alloc test_alloc() noexcept { return eff::testing::test().alloc; }

// A float with a random sign, a random mantissa and an exponent from -12 to
// 11, as probe E4 drew them: the association of a sum changes its bits.
[[nodiscard]] inline float draw_float(::crucible::test::PhiloxStream& stream) noexcept {
    std::uint64_t const bits = stream.next_wide();
    auto const mantissa = static_cast<std::uint32_t>(bits & 0x7FFFFFu);
    auto const exponent = static_cast<std::uint32_t>(127 + (bits >> 23) % 24 - 12);
    auto const sign = static_cast<std::uint32_t>((bits >> 40) & 1u);
    return ::fixy::bit_cast<float>((sign << 31) | (exponent << 23) | mantissa);
}

[[nodiscard]] inline ::foundation::core::HeapArray<float> draw_floats(std::size_t count, std::uint64_t seed) noexcept {
    ::foundation::core::HeapArray<float> run = ::foundation::core::mint_heap_array(test_alloc(), count, 0.0f);
    ::crucible::test::PhiloxStream stream{seed};
    for (float& value : run) {
        value = draw_float(stream);
    }
    return run;
}

[[nodiscard]] inline bool same_bits(float left, float right) noexcept {
    return ::fixy::bit_cast<std::uint32_t>(left) == ::fixy::bit_cast<std::uint32_t>(right);
}

[[nodiscard]] inline bool same_runs(::foundation::core::View<float const> left,
                                    ::foundation::core::View<float const> right) noexcept {
    if (left.size() != right.size()) return false;
    for (auto const [first, second] : left.walk().try_zip(right.walk()).expect("the two runs have one count")) {
        if (!same_bits(*first, *second)) return false;
    }
    return true;
}

// The counts of the runs: no element, one, less than one grain, the counts
// around one grain, and 65 grains with a short last grain.
inline constexpr std::size_t kLongest = (std::size_t{1} << 20) + 7;
inline constexpr ::fixy::FixedArray<std::size_t, 7> kCounts{0, 1, 1000, 16383, 16384, 16385, kLongest};

// The counts of a run of 64 threads: each run starts 64 threads, and a start
// costs about a millisecond under AddressSanitizer.
inline constexpr ::fixy::FixedArray<std::size_t, 3> kWideCounts{0, 16385, kLongest};

}  // namespace crucible::test::parallel_tree
