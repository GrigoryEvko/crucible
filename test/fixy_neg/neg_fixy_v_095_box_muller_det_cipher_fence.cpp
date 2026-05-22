// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FIXY-V-095 HS14 fixture #1 — Cipher-write-fence rejection of the
// libm-pathed `Philox::box_muller_det`.
//
// V-095 downgraded `box_muller_det`'s DetSafe tier from PhiloxRng to
// MonotonicClockRead because the libm sin/cos/log dependencies are
// not cross-platform bit-stable (glibc vs musl vs Apple libm vs MSVC
// CRT all produce different last-ulp results).  The Cipher write-fence
// — the type-level gate that admits replay-deterministic values only —
// accepts PhiloxRng-or-stronger tiers.  Therefore feeding
// `box_muller_det`'s result into a function gated by
// `admissible_at_cipher_fence<W>` MUST fail to compile.
//
// Distinct from FIXY-V-095 HS14 fixture #2 (Pure-tier gate against
// the polynomial variant):
//   * Fixture #1 (this file) — SOURCE is libm path (MonotonicClockRead);
//                              GATE is at PhiloxRng (Cipher write-fence).
//                              Rejection direction: tier strictly weaker
//                              than the boundary.
//   * Fixture #2 — SOURCE is polynomial path (PhiloxRng); GATE is at
//                  Pure (strictly stronger than PhiloxRng).  Rejection
//                  direction: tier weaker than a stricter consumer that
//                  demands fully-pure-function semantics.
//
// Both fixtures share the same lattice-subsumption mechanism but pin
// orthogonal corners of the (source-tier × gate-tier) grid, so HS14's
// "distinct mismatch classes" floor is met.
//
// Expected diagnostic: "constraints not satisfied" /
//                      "admissible_at_cipher_fence" /
//                      "satisfies" / "DetSafeTier_v".

#include <crucible/Philox.h>

namespace cwrap = ::crucible::fixy::wrap;
using crucible::Philox;

namespace neg_fixy_v_095_box_muller_det_cipher_fence {

// Simulate the Cipher write-fence: a generic function template that
// accepts any DetSafe-typed value whose tier subsumes PhiloxRng.
template <typename W>
concept admissible_at_cipher_fence =
    W::template satisfies<cwrap::DetSafeTier_v::PhiloxRng>;

template <typename W>
    requires admissible_at_cipher_fence<W>
[[nodiscard]] constexpr int cipher_write_fence(W const&) noexcept {
    return 1;
}

}  // namespace neg_fixy_v_095_box_muller_det_cipher_fence

int main() {
    namespace fixt = neg_fixy_v_095_box_muller_det_cipher_fence;

    // box_muller_det returns DetSafe<MonotonicClockRead, pair<float,float>>.
    // MonotonicClockRead does NOT subsume PhiloxRng, so the requires-
    // clause must reject this call.
    auto bad = Philox::box_muller_det(0u, 0u);
    [[maybe_unused]] auto fail = fixt::cipher_write_fence(bad);
    return 0;
}
