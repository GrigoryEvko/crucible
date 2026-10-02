// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A stable id holds only for a type whose printed name is a function of
// the type.  A closure type has no declared name, so each id refuses it.
// HasStableIdentity calls the identity walk itself, and a concept has no
// specialization.  Here a translation unit tries to put a verdict of no
// fault in front of the walk, through a variable template of the detail
// namespace.  No such template exists, and the gate refuses the closure.
//
// Expected diagnostic: the static assertion of the stable name refuses
// the closure type, because the closure has no declared name.

#include <foundation/reflect/Hash.h>

#include <cstdint>

inline constexpr auto closure = [] {};
using Closure = decltype(closure);

template <>
inline constexpr foundation::reflect::detail::identity_verdict
    foundation::reflect::detail::identity_verdict_of<Closure> = {};

[[maybe_unused]] inline constexpr std::uint64_t forged_id = foundation::reflect::stable_type_id<Closure>;

int main() { return 0; }
