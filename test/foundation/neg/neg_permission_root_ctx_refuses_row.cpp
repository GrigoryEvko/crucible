// A ctx-bound root mint is admitted only by a context whose row covers
// the tag's row.  The tag here needs IO and the foreground context's
// row is empty, so the one root template's fit concept is not
// satisfied, and the note names the concept with the tag and the
// context.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace {
struct NeedsIo {
    using permission_row = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};

using FgCtx = ::foundation::effects::detail::ctx_witnesses::FgWitness;
}  // namespace

int main() {
    [[maybe_unused]] auto token = ::foundation::permissions::mint_permission_root<NeedsIo>(FgCtx{});
    return 0;
}
