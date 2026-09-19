// A ctx-bound root mint is admitted only by a context whose row covers
// the tag's row.  The tag here needs IO and the foreground context's
// row is empty, so the one root template's fit concept is not
// satisfied, and the note names the concept with the tag and the
// context.

#include <foundation/effects/Ctx.h>
#include <foundation/permissions/Permission.h>

namespace {
struct NeedsIo {};

using FgCtx = ::foundation::effects::detail::exec_ctx_self_test::FgWitness;
}  // namespace

namespace foundation::permissions {
template <>
struct permission_row<NeedsIo> {
    using type = ::foundation::effects::Row<::foundation::effects::Effect::IO>;
};
}  // namespace foundation::permissions

int main() {
    [[maybe_unused]] auto token = ::foundation::permissions::mint_permission_root<NeedsIo>(FgCtx{});
    return 0;
}
