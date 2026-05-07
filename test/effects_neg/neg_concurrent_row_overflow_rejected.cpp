// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #1 of 2 for effects::ConcurrentlySchedulable
// (GAPS-190 #1215).
//
// Premise: Concurrent scheduling of two ops whose pairwise sum on
// any ResourceKind would overflow uint64_t MUST be rejected at
// compile time.  Without this gate, an oversubscribed schedule
// (e.g., two ops each declaring near-UINT64_MAX HbmBytes
// consumption) would silently wrap to a small positive value and
// pass the FitsCog ≤ Cog::TargetCaps check vacuously — the FitsCog
// gate sees `combined ≤ cap` where `combined` is now garbage.
//
// Why this is the load-bearing soundness gate:
//
// Resources budgets are uint64_t precisely because byte-accounted
// axes (HbmBytes, SmemBytes, LlcBytes, HbmBw, NvlinkBw, etc.) carry
// chip-scale magnitudes that exceed uint32_t.  But uint64_t is not
// infinity — UINT64_MAX is reachable in pathological schedules
// (e.g., a future test fixture pinning UINT64_MAX, OR misuse where a
// dataflow analysis emits the type-level "I don't know, assume the
// max" sentinel).  Without ConcurrentlySchedulable's no-overflow
// guard, the wraparound silently converts a "literally
// impossible" schedule into a "trivially possible" one.
//
// Companion fixture: neg_concurrent_row_non_resource_tag.cpp
//   * That one tests rejection at the CARRIER concept gate — a
//     non-ResourceTag in the ConcurrentRow<...> pack fails the
//     ResourceTag concept attached to the template parameter pack.
//     Distinct mismatch class (template substitution failure on the
//     parameter constraint).
//   * This one tests rejection at the SCHEDULING concept gate —
//     the ResourceTag pack is well-formed, every tag value is
//     well-formed, but the pairwise sum on one kind wraps.
//     Distinct mismatch class (overflow detection inside the
//     concept body).
//
// Per HS14, ≥2 negative-compile fixtures per new soundness gate, each
// demonstrating a distinct mismatch class.
//
// Expected diagnostic: "static assertion failed" /
// "ConcurrentlySchedulable" / "constraint not satisfied" /
// "GAPS-190" pointing at the static_assert call site below.

#include <crucible/effects/Concurrent.h>
#include <crucible/effects/Resources.h>

#include <cstdint>

namespace eff = crucible::effects;

// Two rows whose HbmBytes sum overflows uint64_t.  UINT64_MAX + 1
// wraps to 0 — the canonical overflow case.
//
// HbmBytes is byte-accounted; UINT64_MAX bytes = ~16 ExaBytes — far
// beyond any realistic chip.  But the wrap is a genuine soundness
// concern: a future analysis that emits UINT64_MAX as the "unknown
// budget" sentinel would silently saturate to 0 if not gated.
using R_at_max = eff::ConcurrentRow<eff::resource::HbmBytes<UINT64_MAX>>;
using R_plus_one = eff::ConcurrentRow<eff::resource::HbmBytes<1>>;

// ConcurrentlySchedulable<R_at_max, R_plus_one> must NOT be
// satisfied — the pairwise sum overflows.  The static_assert fires.
static_assert(eff::ConcurrentlySchedulable<R_at_max, R_plus_one>,
    "ConcurrentlySchedulable concept must reject schedules whose "
    "pairwise sum overflows uint64_t on any ResourceKind — "
    "GAPS-190 overflow defense compromised.");

int main() { return 0; }
