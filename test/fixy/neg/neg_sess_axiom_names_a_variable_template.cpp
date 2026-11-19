// An axiom of a payload order names a concept in drops and weakens.  A
// translation unit can specialize a variable template, and the order would
// then read the specialization.  This file names a variable template in
// weakens, and the first query of the order refuses the axiom.

#include <fixy/session/Refinement.h>

#include <meta>
#include <type_traits>

template <class T, class U>
inline constexpr bool short_to_long_v = std::is_same_v<T, short> && std::is_same_v<U, long>;

namespace open_axioms {
inline constexpr ::fixy::session::transition::subsort_axiom widen{.weakens = ^^short_to_long_v};
}  // namespace open_axioms

static_assert(::fixy::session::transition::subsorts(^^open_axioms, ^^short, ^^long));

int main() { return 0; }
