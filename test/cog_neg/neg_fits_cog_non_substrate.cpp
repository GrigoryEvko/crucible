// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #2 of 2 for cog::FitsCog (GAPS-191 #1216).
//
// Premise: cog::FitsCog<Row, K> conjuncts on three concept gates —
// IsConcurrentRow<Row> AND HasCogCapacity<K> AND
// row_fits_cog_v<Row, K>.  The middle conjunct, HasCogCapacity<K>,
// holds IFF cog_max_capacity<K> has a class-template specialisation
// (i.e., K is a substrate CogKind).  Calling FitsCog with a NON-
// substrate CogKind atom (PsuRail, BmcSensor, OpticalTransceiver,
// NvmeNamespace, PcieLaneGroup, or any of the L1..L7 aggregate
// kinds) fails the HasCogCapacity conjunct at template substitution
// — the requires-clause refuses substitution and the build fails
// here at the call site.
//
// Why this is the load-bearing soundness gate:
//
// Without the SUBSTRATE-VALIDITY gate, a future GAPS-188
// mint_cog_mimic factory (or GAPS-810 partition optimiser) could
// silently accept PsuRail as a target and produce a Mimic stub
// bound to a Cog with no compute substrate, no memory substrate,
// and no capacity ceilings to compare against.  Subsequent
// scheduling code would either:
//
//   1. Pass FitsCog vacuously (because every demand <= 0 trivially
//      holds for an empty row, and a non-empty row would always
//      fail because every ceiling is 0) — masking real bugs as
//      "fits, but does nothing";
//
//   2. Fall through to a default code path producing a no-op stub
//      that silently drops scheduled work — exactly the "Cog-
//      substrate-binding partition defense compromise" failure
//      mode the GAPS-187 / GAPS-191 layered defenses were
//      designed to prevent.
//
// The HasCogCapacity gate refuses non-substrate Cogs at template
// substitution, so the misuse "ask if Row fits PsuRail" is
// structurally impossible — not "answers no", but "doesn't even
// type-check".  This is the strongest possible defense, since the
// caller can't accidentally swallow a `bool false` result.
//
// Companion fixture: neg_fits_cog_oversubscribed.cpp
//   * That one tests rejection at the QUANTITY gate — FitsCog
//     refuses a Row whose declared demand exceeds the substrate's
//     per-axis ceiling.  Distinct mismatch class (numeric
//     exceedance on a SUBSTRATE-EXPOSED axis).
//   * This one tests rejection at the SUBSTRATE-VALIDITY gate —
//     HasCogCapacity refuses non-substrate CogKind atoms at
//     template substitution.  Distinct mismatch class (structural
//     binding failure: "this Cog has no budget axes at all").
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate,
// each demonstrating a distinct mismatch class.
//
// Expected diagnostic: "constraint not satisfied" / "constraints not
// satisfied" / "FitsCog" / "HasCogCapacity" / "schedule_kernel" /
// "PsuRail" / "GAPS-191" pointing at the call site below.

#include <crucible/cog/FitsCog.h>
#include <crucible/effects/Resources.h>
#include <crucible/effects/Concurrent.h>

namespace cog = crucible::cog;
namespace effects = crucible::effects;

// Mock of the future GAPS-188 / GAPS-810 scheduling shape: a function
// templated on a row-typed budget AND a CogKind atom, constrained on
// cog::FitsCog.  Calling with a non-substrate K fails the embedded
// HasCogCapacity<K> conjunct because cog_max_capacity<PsuRail> is
// never specialised.
template <typename Row, cog::CogKind K>
    requires cog::FitsCog<Row, K>
constexpr int schedule_kernel() noexcept { return 1; }

// A trivially-fitting row.  Even an EMPTY row fails to satisfy
// FitsCog<R, PsuRail> because the HasCogCapacity gate fires before
// any axis comparison — substrate-binding failure, not quantity
// exceedance.  This is the load-bearing distinction from fixture
// #1: an empty row would PASS the oversubscription gate (demand 0
// <= ceiling 0) but FAILS the substrate-validity gate.
using TrivialRow = effects::ConcurrentRow<>;

static_assert(schedule_kernel<TrivialRow, cog::CogKind::PsuRail>() == 1,
    "GAPS-191: cog::FitsCog concept MUST refuse non-substrate CogKind "
    "atoms (PsuRail / BmcSensor / OpticalTransceiver / aggregates) at "
    "template substitution.  If this static_assert ever evaluates, a "
    "future mint_cog_mimic factory or partition optimiser would silently "
    "accept PsuRail as a Mimic target and produce either (a) a no-op "
    "stub that drops scheduled work, or (b) a stub that vacuously "
    "passes FitsCog because every empty-axis demand is trivially <= 0. "
    "The HasCogCapacity gate refuses substitution structurally — the "
    "misuse becomes a compile error at the kernel author's call site, "
    "not a silent runtime corruption.");

int main() { return 0; }
