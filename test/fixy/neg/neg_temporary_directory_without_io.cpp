// mint_temporary_directory makes a directory in the file system, which is IO.
// The foreground context owns no effect, so the gate refuses it.

#include <fixy/os/Fs.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using ForegroundCtx = fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>;

[[maybe_unused]] bool makes(ForegroundCtx const& ctx) { return fixy::fs::mint_temporary_directory(ctx).is_ok(); }

}  // namespace
