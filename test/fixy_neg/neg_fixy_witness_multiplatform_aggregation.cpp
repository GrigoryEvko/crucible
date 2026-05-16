// ── neg_fixy_witness_multiplatform_aggregation (Followup D HS14) ─────
//
// Pins Followup D's multi-platform witness aggregation rule:
// `MultiplatformWitness<PB<W1, P1>, PB<W2, P2>, ...>` satisfies a
// witness-floor demand FOR A SPECIFIC PLATFORM iff at least one
// sub-witness claims evidence valid on that platform AND meets the
// floor.  A consumer demanding the floor for a platform NOT in any
// sub-witness's pack must reject the aggregator.
//
// The fixture constructs an aggregator carrying Tested evidence for
// X86_64 and AArch64 ONLY, then asks "does the aggregator claim
// Tested evidence on RISCV?".  Answer: no — only Asserted floor.  A
// concept demanding `claims_on_platform_v<W, RISCV> AND lattice tier
// ≥ Tested` rejects, and the static_assert inverts the predicate so
// build red fires.
//
// Build red is the EXPECTED outcome.  Matched diagnostic mentions
// "claims_on_platform" or "MultiplatformWitness" or "RISCV".

#include <crucible/safety/witness/Aggregation.h>
#include <crucible/safety/witness/IsWitness.h>
#include <crucible/safety/witness/Witness.h>

namespace sw = crucible::safety::witness;

namespace {

// Consumer-side gate: demands Tested floor AND evidence valid on a
// specific platform.
template <typename W, typename Platform>
concept ClaimsTestedOnPlatform =
    sw::IsWitness<W> &&
    sw::claims_on_platform_v<W, Platform> &&
    sw::WitnessAtLeast<W, sw::Tested<0>>;

using XAndArmTested = sw::MultiplatformWitness<
    sw::PlatformBounded<sw::Tested<11>, sw::arch::X86_64>,
    sw::PlatformBounded<sw::Tested<22>, sw::arch::AArch64>>;

// Sanity: the aggregator claims evidence on both x86 and aarch64.
static_assert(sw::claims_on_platform_v<XAndArmTested, sw::arch::X86_64>);
static_assert(sw::claims_on_platform_v<XAndArmTested, sw::arch::AArch64>);

// Sanity: NO sub-witness claims evidence on RISCV — the aggregator
// returns false on RISCV.
static_assert(!sw::claims_on_platform_v<XAndArmTested, sw::arch::RISCV>);

// THE DISCIPLINE: a consumer demanding Tested-on-RISCV must reject
// this aggregator.  The static_assert INVERTS the predicate so build
// red is the EXPECTED outcome.
static_assert(ClaimsTestedOnPlatform<XAndArmTested, sw::arch::RISCV>,
    "Followup D fixture: MultiplatformWitness aggregating Tested "
    "evidence on X86_64 + AArch64 ONLY does NOT claim Tested evidence "
    "on RISCV.  A consumer demanding Tested-on-RISCV must reject.  "
    "Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
