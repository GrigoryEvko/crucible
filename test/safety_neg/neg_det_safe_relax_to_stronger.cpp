// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling DetSafe<WeakerTier, T>::relax<StrongerTier>()
// when StrongerTier > WeakerTier in the DetSafeLattice.
//
// THIS IS THE LOAD-BEARING REJECTION FOR THE 8TH AXIOM.  Without
// it, a value sourced from a CLOCK READ could be re-typed as
// PhiloxRng (or even Pure) and silently flow into the Cipher
// replay log, defeating the cross-replay determinism guarantee.
// The whole DetSafe wrapper exists to fence exactly this bug class
// at compile time at the call site instead of in the
// `bit_exact_replay_invariant` CI 12 hours later.
//
// Concrete bug-class this catches: a refactor that loosened the
// requires-clause guarding relax<>() — specifically, a slip from
// `DetSafeLattice::leq(WeakerTier, Tier)` to a permissive form —
// would silently allow a MonotonicClockRead-pinned value to claim
// PhiloxRng compliance, defeating the Cipher write-fence (per
// 28_04_2026_effects.md §3.4):
//
//   template <DetSafeTier_v Tier>
//   auto Cipher::record_event(DetSafe<Tier, Event>)
//       requires (Tier == Pure || Tier == PhiloxRng);
//
// Without this rejection, clock reads + entropy reads + arbitrary
// non-deterministic sources could enter the replay log silently
// and the 8th axiom would be no better fenced than it is today
// (CI-only, 12h after the regression lands).
//
// The lattice direction: Pure is at the TOP (strongest replay-
// safety); NDS is at the BOTTOM (weakest).  Going DOWN (Pure →
// PhiloxRng → MonotonicClockRead → ... → NDS) is allowed —
// stronger replay-safety trivially serves weaker requirement.
// Going UP (MonotonicClockRead → PhiloxRng → Pure) is FORBIDDEN
// — would CLAIM more replay-safety than the source provides.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on relax<>().

#include <crucible/safety/DetSafe.h>

using namespace crucible::safety;

int main() {
    // Pinned at MonotonicClockRead — bytes derive from steady_clock::now().
    // This is what Cipher::record_event MUST reject; the relax<> below
    // is the bug-introduction path the wrapper fences.
    DetSafe<DetSafeTier_v::MonotonicClockRead, int> mono_value{42};

    // Should FAIL: relax<PhiloxRng> on a MonotonicClockRead-pinned
    // wrapper.  The requires-clause `DetSafeLattice::leq(PhiloxRng,
    // MonotonicClockRead)` is FALSE — PhiloxRng is above
    // MonotonicClockRead in the chain — so the relax<> overload is
    // excluded.  Without this fence, a clock-derived value could
    // claim PhiloxRng compliance and silently enter the Cipher
    // replay log, breaking cross-replay determinism.
    auto philox_claim = std::move(mono_value).relax<DetSafeTier_v::PhiloxRng>();
    return philox_claim.peek();
}
