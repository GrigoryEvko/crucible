// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A stable id holds only for a type whose printed name is a function of
// the type.  A closure type has no declared name, so each id refuses it.
// HasStableIdentity calls the identity walk itself, and a concept has no
// specialization.  Here a translation unit tries to put a verdict of no
// fault in front of the walk, through a variable template of the detail
// namespace.  No such template exists.  The fixtures
// neg_stable_id_of_a_closure_invoker_value and test/layer/checks show that
// each id refuses a closure.
//
// Expected diagnostic: the parser refuses the specialization, because the
// namespace declares no such template.

#include <foundation/reflect/Hash.h>

inline constexpr auto closure = [] {};
using Closure = decltype(closure);

template <>
inline constexpr foundation::reflect::detail::identity_verdict
    foundation::reflect::detail::identity_verdict_of<Closure> = foundation::reflect::detail::identity_verdict();

int main() { return 0; }
