// ── test_fixy_perf_v2 — sentinel TU for fixy/perf/V2.h ────────────
//
// FIXY-U-122.  Pulls fixy/perf/V2.h into a TU compiled under project
// warning flags so the header's static_asserts (type-identity +
// concept-resolution + cardinality witness) execute.
//
//   1. fixy::perf::v2::mint_sense_hub_v2 aliases substrate.
//   2. Class-type aliases (SenseHubV2 / CounterSnapshot / CounterDelta
//      / GaugeSnapshot / FullSnapshot / LoadReport / Idx / Gauge)
//      preserve substrate identity.
//   3. CtxFitsSenseHubV2Mint admits ColdInitCtx and rejects both
//      BgDrainCtx (wrong row) and HotFgCtx (empty row).
//   4. Cardinality witness — `fixy::perf::v2::` surfaces exactly 1
//      mint factory.
//
// CRITICAL: this TU MUST NOT include `<crucible/fixy/Perf.h>` or
// `<crucible/perf/SenseHub.h>` — both would re-enter `namespace
// crucible::perf` with conflicting `Idx` / `NUM_COUNTERS` / `Gauge`
// definitions.  The sibling TU `test_fixy_perf.cpp` covers V1; this
// one covers V2; they intentionally compile under DIFFERENT include
// sets.

#include <crucible/fixy/perf/V2.h>

#include <crucible/effects/ExecCtx.h>

#include <type_traits>

namespace fpv2    = ::crucible::fixy::perf::v2;
namespace perf_   = ::crucible::perf;
namespace eff     = ::crucible::effects;

// ─── 1. Function-template identity — mint_sense_hub_v2 ────────────

static_assert(std::is_same_v<
    decltype(&fpv2::mint_sense_hub_v2<eff::ColdInitCtx>),
    decltype(&perf_::mint_sense_hub_v2<eff::ColdInitCtx>)>,
    "FIXY-U-122: fixy::perf::v2::mint_sense_hub_v2 must be the "
    "substrate function (using-decl preserves crucible::perf:: "
    "residency).");

// ─── 2. Type-alias identity ───────────────────────────────────────

static_assert(std::is_same_v<fpv2::SenseHubV2, perf_::SenseHubV2>,
    "fixy::perf::v2::SenseHubV2 must alias substrate.");

static_assert(std::is_same_v<fpv2::CounterSnapshot, perf_::CounterSnapshot>,
    "fixy::perf::v2::CounterSnapshot must alias substrate.");

static_assert(std::is_same_v<fpv2::CounterDelta, perf_::CounterDelta>,
    "fixy::perf::v2::CounterDelta must alias substrate.");

static_assert(std::is_same_v<fpv2::GaugeSnapshot, perf_::GaugeSnapshot>,
    "fixy::perf::v2::GaugeSnapshot must alias substrate.");

static_assert(std::is_same_v<fpv2::FullSnapshot, perf_::FullSnapshot>,
    "fixy::perf::v2::FullSnapshot must alias substrate.");

static_assert(std::is_same_v<fpv2::LoadReport, perf_::LoadReport>,
    "fixy::perf::v2::LoadReport must alias substrate.");

static_assert(std::is_same_v<fpv2::Idx, perf_::Idx>,
    "fixy::perf::v2::Idx must alias substrate.");

static_assert(std::is_same_v<fpv2::Gauge, perf_::Gauge>,
    "fixy::perf::v2::Gauge must alias substrate.");

// ─── 3. Concept-resolution identity ───────────────────────────────

static_assert(fpv2::CtxFitsSenseHubV2Mint<eff::ColdInitCtx>);
static_assert(!fpv2::CtxFitsSenseHubV2Mint<eff::BgDrainCtx>);
static_assert(!fpv2::CtxFitsSenseHubV2Mint<eff::HotFgCtx>);

// ─── 4. Cardinality witness ───────────────────────────────────────
//
// Locks the V2.h sentinel's stated mint cardinality against drift.
// Adding a second V2 mint must touch BOTH fixy/perf/V2.h's
// `v2_mint_cardinality` constant AND this TU's expected value;
// otherwise CI reds.

static_assert(::crucible::fixy::perf::v2::self_test::v2_mint_cardinality == 1,
    "fixy::perf::v2:: mint cardinality drifted from 1 — fixy/perf/V2.h "
    "sentinel block and this TU must update in lockstep.");

int main() {
    // No runtime bpf() invocation under CI sandbox — the smoke is
    // type-level above.  Touch the runtime smoke block so the
    // header's no-throw smoke-test runs under the project's preset
    // semantics.
    ::crucible::fixy::perf::v2::runtime_smoke_test();
    return 0;
}
