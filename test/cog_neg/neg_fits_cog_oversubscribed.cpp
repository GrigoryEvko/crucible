// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for cog::FitsCog.
//
// Premise: cog::FitsCog<Row, K> is a constrained concept admitting
// IFF every per-axis demand declared in Row fits within K's compile-
// time ceiling.  When a constrained template parameterised on
// FitsCog<Row, CogKind::Gpu> is invoked with a Row demanding more SMs
// than ANY shipped GPU substrate has (ceiling = 320, demand = 999),
// concept substitution fails — the constrained call site is ill-
// formed and the build fails here.
//
// Why this is the load-bearing soundness gate:
//
// Without the FitsCog quantity gate, an oversubscribed schedule slips
// through type-checking and reaches the runtime scheduler, where it
// either (a) silently degrades because the scheduler tries to fit a
// 999-SM workload into 132 SMs by serialising launches (no error,
// just observed-throughput-loss-vs-claim), or (b) fails late inside
// a vendor backend with a kernel-emit error far from the source of
// the misuse — confusing, hard-to-trace, and only on real silicon.
//
// The compile-time gate flags the bug at the kernel author's source
// location, before any kernel ever runs, on every supported toolchain.
//
// Companion fixture: neg_fits_cog_non_substrate.cpp
//   * That one tests rejection at the SUBSTRATE-VALIDITY gate —
//     HasCogCapacity<K> refuses non-substrate CogKind atoms (PsuRail
//     / BmcSensor / aggregates) at template substitution.  Distinct
//     mismatch class (structural binding failure: "this Cog has no
//     budget axes at all").
//   * This one tests rejection at the QUANTITY gate — FitsCog refuses
//     a Row whose declared demand exceeds the substrate's per-axis
//     ceiling.  Distinct mismatch class (numeric exceedance on a
//     SUBSTRATE-EXPOSED axis: "this Cog HAS this axis but not enough
//     of it").
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: no matching function for the call of
// schedule_kernel<OversubscribedSmRow, CogKind::Gpu>() below, because
// row_fits_cog_v<Row, K> evaluates to false for the SmBudget<999> row.

#include <crucible/cog/FitsCog.h>
#include <foundation/effects/Concurrent.h>
#include <foundation/effects/Resources.h>

namespace cog = crucible::cog;
namespace effects = ::foundation::effects;

// Mock of the scheduling shape of a future mint_cog_mimic factory or
// partition optimiser: a function templated on a row-typed
// budget AND a CogKind atom, constrained on cog::FitsCog<Row, K>.
// Calling it with a Row whose declared demand exceeds the Cog's
// per-axis ceiling fails the concept gate at substitution time.
template <typename Row, cog::CogKind K>
    requires cog::FitsCog<Row, K>
constexpr int schedule_kernel() noexcept {
    return 1;
}

// A row demanding 999 SMs.  GPU's compile-time ceiling is 320 (the
// largest shipped GPU + headroom).  999 > 320 → FitsCog gate refuses
// substitution.  Any future GPU SKU that legitimately ships with 1000+
// SMs requires bumping the cog_max_capacity<CogKind::Gpu> Sm-axis
// ceiling FIRST.  The doc-block of cog_max_capacity in cog/FitsCog.h
// says why: a higher ceiling breaks nothing, and a lower one rejects
// rows that used to fit.
using OversubscribedSmRow = effects::ConcurrentRow<effects::SmBudget<999>>;

static_assert(schedule_kernel<OversubscribedSmRow, cog::CogKind::Gpu>() == 1,
              "cog::FitsCog concept MUST refuse Rows whose declared "
              "per-axis demand exceeds the Cog's compile-time ceiling.  If this "
              "static_assert ever evaluates, an oversubscribed schedule (999 SMs "
              "demanded vs 320 max on any shipped GPU) would slip past the "
              "compile-time admission gate and reach the runtime scheduler, "
              "where it would silently degrade throughput or fail late inside a "
              "vendor backend.  The gate stops the bug at the kernel author's "
              "source location.");

int main() { return 0; }
