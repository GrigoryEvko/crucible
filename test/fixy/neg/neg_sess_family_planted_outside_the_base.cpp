// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A family of the registry of the session layer is a declaration of the
// base (fixy/session/Registry.h).  A family that a file outside the base
// plants with its own seal, for a shape of its own file, passes each check
// of the registry.  The plugin rule base-namespace refuses it, because a
// file outside the base declares nothing in a namespace of the base.  This
// file has no opt-out region, so the rule sees the declaration of the
// family.
//
// Expected diagnostic: the file declares the registration in a namespace
// of the base, outside the base.

#include <fixy/session/Protocol.h>

namespace planted_family_types {
struct Halt {};
}  // namespace planted_family_types

namespace fixy::session::combinators::planted {
inline constexpr ::fixy::session::transition::combinator halt{.shape = ^^::planted_family_types::Halt,
                                                              .kind = ::fixy::session::transition::shape_kind::terminal,
                                                              .dual = ^^::planted_family_types::Halt};
}  // namespace fixy::session::combinators::planted

int main() { return 0; }
