// Negative-compile test.  This file must fail to compile.
//
// The constructor of a Vigil is private, and the two mints are its friends.
// A direct construction would start a Vigil with no context of a caller.

#include <crucible/Vigil.h>
#include <fixy/Ctx.h>
#include <foundation/effects/Effect.h>

int main() {
    ::fixy::InitLoadCtx const startup{::foundation::effects::testing::init()};
    crucible::Vigil vigil{crucible::Vigil::Config{}, startup, ::fixy::Option<crucible::warden::Policy>{::fixy::none}};
    return vigil.is_compiled() ? 0 : 1;
}
