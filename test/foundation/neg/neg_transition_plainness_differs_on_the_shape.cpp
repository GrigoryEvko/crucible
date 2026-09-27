// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A marker that no plain protocol holds has a dual that a plain protocol
// holds.  The dual of a protocol that no plain protocol holds would then
// be well-formed, so the coherence check of the transition algebra
// refuses the pair.  This registry puts the marker that is not plain
// first, so the check names it.  A layer refuses an incoherent
// registration where a query first meets it, with the message of the
// algebra, as this assertion does.
//
// Expected diagnostic: the registration of Mark is incoherent, because
// its dual disagrees on plainness.
#include <foundation/algebra/Transition.h>

namespace tr = ::foundation::algebra::transition;

namespace neg_transition_plainness_differs_on_the_shape_types {
template <class K>
struct Mark {};
template <class K>
struct Unmark {};

namespace registry {
inline constexpr tr::combinator mark{.shape = ^^Mark, .kind = tr::shape_kind::marker, .dual = ^^Unmark, .is_plain = false};
inline constexpr tr::combinator unmark{.shape = ^^Unmark, .kind = tr::shape_kind::marker, .dual = ^^Mark};
}  // namespace registry
}  // namespace neg_transition_plainness_differs_on_the_shape_types

static_assert(tr::check_registry(^^neg_transition_plainness_differs_on_the_shape_types::registry).reason
                  == tr::incoherence::none,
              tr::incoherent_message("layer [Incoherent_Registration]: ",
                                     tr::check_registry(^^neg_transition_plainness_differs_on_the_shape_types::registry)));

int main() { return 0; }
