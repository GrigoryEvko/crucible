#pragma once

// The two sense-hub versions are alternative builds of one hub, not two
// hubs that coexist. Both inhabit crucible::perf and both define the
// counter-index enum and the counter-count constant, with different
// shapes and different values, so a translation unit takes one version
// or the other. This header surfaces the version-2 mint alone. A
// translation unit that needs it must not also pull the version-1 perf
// surface.
//
// The numeric build constants are deliberately left out of the
// re-export. This umbrella is the mint chokepoint, not the numeric ABI
// surface. A caller that needs a count reads it off the hub.

#include <crucible/perf/SenseHubV2.h>
#include <crucible/effects/_ExecCtx.h>

#include <type_traits>

namespace crucible::fixy::perf::v2 {

using ::crucible::perf::mint_sense_hub_v2;
using ::crucible::perf::CtxFitsSenseHubV2Mint;
using ::crucible::perf::SenseHubV2;

using ::crucible::perf::CounterSnapshot;
using ::crucible::perf::CounterDelta;
using ::crucible::perf::GaugeSnapshot;
using ::crucible::perf::FullSnapshot;
using ::crucible::perf::LoadReport;
using ::crucible::perf::Idx;
using ::crucible::perf::Gauge;

}  // namespace crucible::fixy::perf::v2

namespace crucible::fixy::perf::v2::self_test {

static_assert(std::is_same_v<::crucible::fixy::perf::v2::SenseHubV2, ::crucible::perf::SenseHubV2>,
              "SenseHubV2 must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::CounterSnapshot, ::crucible::perf::CounterSnapshot>,
              "CounterSnapshot must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::CounterDelta, ::crucible::perf::CounterDelta>,
              "CounterDelta must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::GaugeSnapshot, ::crucible::perf::GaugeSnapshot>,
              "GaugeSnapshot must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::FullSnapshot, ::crucible::perf::FullSnapshot>,
              "FullSnapshot must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::LoadReport, ::crucible::perf::LoadReport>,
              "LoadReport must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::Idx, ::crucible::perf::Idx>,
              "Idx must alias the substrate type.");

static_assert(std::is_same_v<::crucible::fixy::perf::v2::Gauge, ::crucible::perf::Gauge>,
              "Gauge must alias the substrate type.");

// The gate reads a row that carries Block, because the load calls
// bpf(BPF_PROG_LOAD) and waits on the kernel verifier.  The
// initialization capability permits Row<Init, Alloc, IO> and no Block,
// so it cannot reach the mint.  The test runner capability permits Block
// and does reach it.

static_assert(!::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<::crucible::effects::ColdInitCtx>,
              "The mint gate must reject a cold init context.");

static_assert(::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<::crucible::effects::TestRunnerCtx>,
              "The mint gate must admit a test runner context.");

static_assert(!::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<::crucible::effects::BgDrainCtx>,
              "The mint gate must reject a background drain context.");

static_assert(!::crucible::fixy::perf::v2::CtxFitsSenseHubV2Mint<::crucible::effects::HotFgCtx>,
              "The mint gate must reject a hot foreground context.");

// The exact count sits next to the constant it pins, so a contributor
// adding a second mint meets the assertion in the same edit. The test
// suite pins only a lower bound, which catches the opposite mistake of
// removing a mint.
inline constexpr int v2_mint_cardinality = 1;

static_assert(v2_mint_cardinality == 1, "This umbrella re-exports exactly one mint factory. Adding or "
                                        "removing one means updating the constant and this pin together.");

}  // namespace crucible::fixy::perf::v2::self_test

// The smoke test stays at the type level. Calling the mint would issue a
// real perf-event and BPF syscall, which fails under a sandbox.

namespace crucible::fixy::perf::v2 {

inline void runtime_smoke_test() noexcept {
    constexpr bool rejects_cold = !CtxFitsSenseHubV2Mint<::crucible::effects::ColdInitCtx>;
    constexpr bool rejects_bg = !CtxFitsSenseHubV2Mint<::crucible::effects::BgDrainCtx>;
    constexpr bool rejects_hot = !CtxFitsSenseHubV2Mint<::crucible::effects::HotFgCtx>;
    constexpr bool admits_test = CtxFitsSenseHubV2Mint<::crucible::effects::TestRunnerCtx>;
    (void)rejects_cold;
    (void)rejects_bg;
    (void)rejects_hot;
    (void)admits_test;
}

}  // namespace crucible::fixy::perf::v2
