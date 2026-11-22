// The body of with_library gives a function of its library.  The end of the
// scope closes the library and unmaps the code of the function, so a call of
// the function after the scope would run unmapped code.  The result of the
// body names the brand of the scope, and the refused twin of the door gives
// the reason.

#include <fixy/os/Loader.h>

#include <foundation/core/Ref.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Effect.h>

namespace fe = ::foundation::effects;

namespace {

using IoBlockCtx = fe::ExecCtx<fe::Test, fe::Row<fe::Effect::Test, fe::Effect::IO, fe::Effect::Block>>;

[[maybe_unused]] bool keeps_function(IoBlockCtx const& ctx, ::fixy::os::LoaderDiagnostic& diagnostic) {
    auto const kept = ::fixy::os::with_library(ctx, "libc.so.6", ::foundation::core::to(diagnostic),
                                               [&ctx, &diagnostic](auto const& library) noexcept {
                                                   return ::fixy::os::find_function<int() noexcept>(
                                                       ctx, library, "getpid", ::foundation::core::to(diagnostic));
                                               });
    return kept.is_ok();
}

}  // namespace
