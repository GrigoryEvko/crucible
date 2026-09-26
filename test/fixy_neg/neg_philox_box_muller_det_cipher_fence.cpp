// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The Cipher write fence refuses the library form of Philox::box_muller_det.
//
// box_muller_det carries the MonotonicClockRead tier, because the library
// sin, cos and log differ in the last units in the last place between
// implementations, so its bytes replay only on the machine that produced
// them.  The Cipher write fence admits a value at PhiloxRng or stronger.
// Passing box_muller_det's result to a function gated on that fence must
// fail to compile.
//
// The sibling fixture neg_philox_box_muller_polynomial_det_wrong_tier_consume
// pins the other corner: a PhiloxRng source refused by a Pure gate.  This
// one has a source below the fence, that one a gate above the source.

#include <crucible/Philox.h>
#include <fixy/Bands.h>

using crucible::Philox;

namespace neg_philox_box_muller_det_cipher_fence {

// Stands in for the Cipher write fence: any DetSafe band whose tier
// subsumes PhiloxRng.
template <typename W>
concept admissible_at_cipher_fence = ::fixy::satisfies_v<W, ::fixy::DetSafeTier_v::PhiloxRng>;

template <typename W>
    requires admissible_at_cipher_fence<W>
[[nodiscard]] constexpr int cipher_write_fence(W const&) noexcept {
    return 1;
}

}  // namespace neg_philox_box_muller_det_cipher_fence

int main() {
    namespace fixt = neg_philox_box_muller_det_cipher_fence;

    // box_muller_det returns DetSafe<MonotonicClockRead, pair<float, float>>.
    // MonotonicClockRead does not subsume PhiloxRng, so the requires-clause
    // refuses this call.
    auto bad = Philox::box_muller_det(0u, 0u);
    [[maybe_unused]] auto fail = fixt::cipher_write_fence(bad);
    return 0;
}
