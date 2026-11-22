// with_library waits for the disk while it opens and maps the files of a
// library, so it asks for Block beside IO.  A context that owns IO and not
// Block is refused.

#include <fixy/os/Loader.h>

#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO>>;

[[maybe_unused]] bool loads(IoCtx const& ctx, fixy::os::LoaderDiagnostic& diagnostic) {
    return fixy::os::with_library(ctx, "libc.so.6", ::foundation::core::to(diagnostic), [](auto const&) noexcept {})
        .is_ok();
}

}  // namespace
