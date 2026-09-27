// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A plain marker has a dual that no plain protocol holds.  The dual of a
// well-formed protocol would then not be well-formed, so the coherence
// check of the transition algebra refuses the pair.  This registry puts
// the plain marker first, so the check names it: the rule holds from each
// side of the pair.
//
// Expected diagnostic: the registration of Unmark is incoherent, because
// its dual disagrees on plainness.
#include <foundation/algebra/Transition.h>

namespace tr = ::foundation::algebra::transition;

namespace neg_transition_plainness_differs_on_the_dual_types {
template <class K>
struct Mark {};
template <class K>
struct Unmark {};

namespace registry {
inline constexpr tr::combinator unmark{.shape = ^^Unmark, .kind = tr::shape_kind::marker, .dual = ^^Mark};
inline constexpr tr::combinator mark{.shape = ^^Mark, .kind = tr::shape_kind::marker, .dual = ^^Unmark, .is_plain = false};
}  // namespace registry
}  // namespace neg_transition_plainness_differs_on_the_dual_types

static_assert(tr::check_registry(^^neg_transition_plainness_differs_on_the_dual_types::registry).reason
                  == tr::incoherence::none,
              tr::incoherent_message("layer [Incoherent_Registration]: ",
                                     tr::check_registry(^^neg_transition_plainness_differs_on_the_dual_types::registry)));

int main() { return 0; }
