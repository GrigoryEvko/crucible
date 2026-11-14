// mint_temporary_file_at makes a file in the file system, which is IO.  The
// foreground context owns no effect, so the gate refuses it.

#include <fixy/os/Fs.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using ForegroundCtx = fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>;

[[maybe_unused]] bool opens(ForegroundCtx const& ctx, fixy::fs::Dirfd const& dir) {
    return fixy::fs::mint_temporary_file_at(ctx, dir).is_ok();
}

}  // namespace
