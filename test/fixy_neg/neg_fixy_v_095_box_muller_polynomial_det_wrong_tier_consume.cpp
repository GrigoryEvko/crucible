// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-095 HS14 fixture #2 — Pure-tier consumer rejection of the
// polynomial-pathed `Philox::box_muller_polynomial_det`.
//
// V-095 added `box_muller_polynomial_det` as a PhiloxRng-tier
// replacement for the now-MonotonicClockRead `box_muller_det`.  The
// polynomial variant uses crucible-source polynomial transcendentals
// plus IEEE 754 correctly-rounded `std::sqrt` and IS admissible at the
// Cipher write-fence (PhiloxRng boundary).  But the lattice ALSO has a
// strictly-stronger Pure tier reserved for fully-replayable pure-
// function results (no input dependency on any external state, no
// floating-point transcendentals at all).  A consumer gated at
// `satisfies<Pure>` MUST reject the polynomial Box-Muller because
// polynomial sin/cos/log are still float-arithmetic and depend on the
// IEEE 754 hardware rounding mode (even when correctly-rounded).
//
// This pins the tier-upgrade boundary EXACTLY at PhiloxRng — V-095's
// premise depends on the polynomial path being WEAKER than Pure (so
// op_key_det and similar Pure-tier values are not accidentally
// downgraded) while STRONGER than MonotonicClockRead (so it actually
// passes the Cipher fence).
//
// Distinct from FIXY-V-095 HS14 fixture #1 (Cipher-fence rejection of
// libm Box-Muller):
//   * Fixture #1 — SOURCE is libm path (MonotonicClockRead); GATE is
//                  at PhiloxRng.  Rejects because source-tier is
//                  strictly below the boundary.
//   * Fixture #2 (this file) — SOURCE is polynomial path (PhiloxRng);
//                              GATE is at Pure.  Rejects because the
//                              gate demands a tier strictly stronger
//                              than the polynomial path can provide.
//
// Both fixtures exercise the same lattice-subsumption mechanism but at
// orthogonal (source-tier, gate-tier) corners of the lattice — HS14's
// "distinct mismatch classes" floor satisfied.
//
// Expected diagnostic: "constraints not satisfied" /
//                      "requires_pure_tier" / "satisfies" /
//                      "DetSafeTier_v::Pure".

#include <crucible/Philox.h>

namespace cwrap = ::crucible::fixy::wrap;
using crucible::Philox;

namespace neg_fixy_v_095_box_muller_polynomial_det_wrong_tier_consume {

// A stricter consumer than the Cipher write-fence: demands Pure tier.
// Used by Forge phases / op_key derivation paths that compose
// algebraically without any FP transcendentals.
template <typename W>
concept requires_pure_tier =
    W::template satisfies<cwrap::DetSafeTier_v::Pure>;

template <typename W>
    requires requires_pure_tier<W>
[[nodiscard]] constexpr int pure_only_consumer(W const&) noexcept {
    return 1;
}

}  // namespace neg_fixy_v_095_box_muller_polynomial_det_wrong_tier_consume

int main() {
    namespace fixt =
        neg_fixy_v_095_box_muller_polynomial_det_wrong_tier_consume;

    // box_muller_polynomial_det returns DetSafe<PhiloxRng, pair<float,float>>.
    // PhiloxRng does NOT subsume Pure (PhiloxRng < Pure in the lattice),
    // so the requires-clause must reject this call.
    auto poly = Philox::box_muller_polynomial_det(0u, 0u);
    [[maybe_unused]] auto fail = fixt::pure_only_consumer(poly);
    return 0;
}
