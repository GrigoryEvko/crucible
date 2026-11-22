// The caller names the body of a dispatch as an lvalue, so it keeps an object
// that it could call again past the window.  The door takes its body by move.
#include <fixy/concurrent/Dispatch.h>
#include <foundation/core/RootKeys.h>
#include <foundation/effects/Ctx.h>
namespace {
struct[[= ::foundation::core::dispatch_brand{}]] LogBrand final {};
struct Owner final {};
using DispatchCtx =
    ::foundation::effects::ExecCtx<::foundation::effects::ctx_cap::BrandedFg<Owner>, ::foundation::effects::Row<>>;
}  // namespace
int main() {
    DispatchCtx const ctx = ::foundation::effects::testing::foreground<Owner>();
    auto body = [](DispatchCtx const&, ::foundation::core::LoanKey<LogBrand>&) noexcept {};
    ::fixy::dispatch::run_dispatch(ctx, body);
    return 0;
}
