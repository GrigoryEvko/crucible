// A body of the dispatch door moves its key into an object that it keeps, so
// the key would open frames past the window of the dispatch.  A key of a
// dispatch brand does not move.
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
    ::fixy::dispatch::run_dispatch(ctx, [](DispatchCtx const&, ::foundation::core::LoanKey<LogBrand>& key) noexcept {
        ::foundation::core::LoanKey<LogBrand> kept{std::move(key)};
        static_cast<void>(kept);
    });
    return 0;
}
