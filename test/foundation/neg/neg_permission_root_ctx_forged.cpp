// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A ctx-bound mint reads the row of its context through row_type.  Any
// class can declare a row_type, so a class that is not an execution
// context could otherwise claim the IO effect and mint a token for a
// region whose touches do IO.  CtxAdmitsPermission asks for an execution
// context first, so the root mint's fit concept refuses the look-alike.
//
// Expected diagnostic: the root mint's fit concept is not satisfied.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace {
struct NeedsIo {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

struct LooksLikeACtx {
    using row_type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace

int main() {
    [[maybe_unused]] auto token = ::foundation::permissions::mint_permission_root<NeedsIo>(LooksLikeACtx{});
    return 0;
}
