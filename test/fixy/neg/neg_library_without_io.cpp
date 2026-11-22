// with_library opens and maps the files of a library, which is IO.  The
// foreground context owns no effect, so the gate refuses it.

#include <fixy/os/Loader.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using ForegroundCtx = fe::ExecCtx<fe::ctx_cap::Fg, fe::Row<>>;

[[maybe_unused]] bool loads(ForegroundCtx const& ctx, fixy::os::LoaderDiagnostic& diagnostic) {
    return fixy::os::with_library(ctx, "libc.so.6", ::foundation::core::to(diagnostic), [](auto const&) noexcept {})
        .is_ok();
}

}  // namespace
