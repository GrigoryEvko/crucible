// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A Pure consumer refuses the polynomial form of Philox::box_muller_polynomial_det.
//
// box_muller_polynomial_det carries the PhiloxRng tier: its sin, cos and
// log are in-tree polynomials and its square root is correctly rounded, so
// it passes the Cipher write fence.  The lattice also has the stronger
// Pure tier, for values that depend on declared inputs alone.  A consumer
// that demands Pure must refuse the polynomial result, which pins the
// boundary of that path exactly at PhiloxRng: stronger than the library
// form, weaker than Pure.
//
// The sibling fixture neg_philox_box_muller_det_cipher_fence pins the other
// corner: a source below the Cipher fence.  This one has a gate above the
// source.

#include <crucible/Philox.h>
#include <fixy/Bands.h>

using crucible::Philox;

namespace neg_philox_box_muller_polynomial_det_wrong_tier_consume {

// A stricter consumer than the Cipher write fence: it demands Pure.
template <typename W>
concept requires_pure_tier = ::fixy::satisfies_v<W, ::fixy::DetSafeTier_v::Pure>;

template <typename W>
    requires requires_pure_tier<W>
[[nodiscard]] constexpr int pure_only_consumer(W const&) noexcept {
    return 1;
}

}  // namespace neg_philox_box_muller_polynomial_det_wrong_tier_consume

int main() {
    namespace fixt = neg_philox_box_muller_polynomial_det_wrong_tier_consume;

    // box_muller_polynomial_det returns DetSafe<PhiloxRng, pair<float, float>>.
    // PhiloxRng sits below Pure, so the requires-clause refuses this call.
    auto poly = Philox::box_muller_polynomial_det(0u, 0u);
    [[maybe_unused]] auto fail = fixt::pure_only_consumer(poly);
    return 0;
}
