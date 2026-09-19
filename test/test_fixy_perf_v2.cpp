// Sentinel TU: compiles the alias header under the project warning flags so its
// static_asserts run.
//
// Do not include <crucible/fixy/Perf.h> or <crucible/perf/SenseHub.h> here.
// Either one re-enters namespace crucible::perf with conflicting Idx,
// NUM_COUNTERS and Gauge definitions.  The V1 surface is covered by a separate
// TU that compiles under a different include set.

#include <crucible/fixy/perf/V2.h>

#include <crucible/effects/_ExecCtx.h>

#include <type_traits>

namespace fpv2 = ::crucible::fixy::perf::v2;
namespace perf_ = ::crucible::perf;
namespace eff = ::crucible::effects;

static_assert(std::is_same_v<decltype(&fpv2::mint_sense_hub_v2<eff::ColdInitCtx>),
                             decltype(&perf_::mint_sense_hub_v2<eff::ColdInitCtx>)>,
              "fixy::perf::v2::mint_sense_hub_v2 must be the substrate function "
              "(the using-declaration preserves crucible::perf:: residency).");

static_assert(std::is_same_v<fpv2::SenseHubV2, perf_::SenseHubV2>, "fixy::perf::v2::SenseHubV2 must alias substrate.");

static_assert(std::is_same_v<fpv2::CounterSnapshot, perf_::CounterSnapshot>,
              "fixy::perf::v2::CounterSnapshot must alias substrate.");

static_assert(std::is_same_v<fpv2::CounterDelta, perf_::CounterDelta>,
              "fixy::perf::v2::CounterDelta must alias substrate.");

static_assert(std::is_same_v<fpv2::GaugeSnapshot, perf_::GaugeSnapshot>,
              "fixy::perf::v2::GaugeSnapshot must alias substrate.");

static_assert(std::is_same_v<fpv2::FullSnapshot, perf_::FullSnapshot>,
              "fixy::perf::v2::FullSnapshot must alias substrate.");

static_assert(std::is_same_v<fpv2::LoadReport, perf_::LoadReport>, "fixy::perf::v2::LoadReport must alias substrate.");

static_assert(std::is_same_v<fpv2::Idx, perf_::Idx>, "fixy::perf::v2::Idx must alias substrate.");

static_assert(std::is_same_v<fpv2::Gauge, perf_::Gauge>, "fixy::perf::v2::Gauge must alias substrate.");

static_assert(fpv2::CtxFitsSenseHubV2Mint<eff::ColdInitCtx>);
static_assert(!fpv2::CtxFitsSenseHubV2Mint<eff::BgDrainCtx>);
static_assert(!fpv2::CtxFitsSenseHubV2Mint<eff::HotFgCtx>);

// This is a floor, not an equality.  The exact pin sits beside the
// source-of-truth constant in the header.  A floor here catches only the inverse
// direction, a mint removed from the surface.  Growth past the floor is silent
// and the header equality tracks it.

static_assert(::crucible::fixy::perf::v2::self_test::v2_mint_cardinality >= 1,
              "floor: the fixy::perf::v2 mint cardinality is below 1 — mint_sense_hub_v2 "
              "was removed without updating the colocated ceiling pin and this floor "
              "witness.");

int main() {
    // No bpf() call runs here.  The smoke test only exercises the header body
    // under the project preset semantics.
    ::crucible::fixy::perf::v2::runtime_smoke_test();
    return 0;
}
