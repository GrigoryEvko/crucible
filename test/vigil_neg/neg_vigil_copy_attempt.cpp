// Negative-compile test.  This file must fail to compile.
//
// Vigil owns the publication slot, ModeCell, foreground
// context, and background thread. Copying it would duplicate those process-local
// identities and split the bg->fg publication discipline.

#include <crucible/Vigil.h>

int main() {
    ::crucible::Vigil first = ::crucible::mint_vigil(::fixy::InitLoadCtx{::foundation::effects::testing::init()}, {});
    ::crucible::Vigil second{first};
    return second.is_compiled() ? 0 : 1;
}
